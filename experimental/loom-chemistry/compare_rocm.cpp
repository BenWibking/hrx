// SPDX-License-Identifier: BSD-3-Clause
// HIP runtime comparison of the original kernels and Loom-generated HSACO.
#define PRIMORDIAL_ROS2S_ENABLE_HIP 1
#define PRIMORDIAL_ROS2S_NO_MAIN 1
#include "reference.cpp"
#include "reproducer.cpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace lc = chemistry;
static_assert(sizeof(CollapseState) == sizeof(lc::CellRecord));
static_assert(sizeof(lc::CellRecord) == 216);

static void hip_check(hipError_t result, const char* action) {
    if (result != hipSuccess) throw std::runtime_error(std::string(action) + ": " + hipGetErrorString(result));
}

template <typename T> struct DeviceBuffer {
    T* ptr{};
    explicit DeviceBuffer(std::size_t count) { hip_check(hipMalloc(&ptr, count * sizeof(T)), "hipMalloc"); }
    ~DeviceBuffer() { if (ptr) hipFree(ptr); }
    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;
    void put(const T* src, std::size_t count) { hip_check(hipMemcpy(ptr, src, count * sizeof(T), hipMemcpyHostToDevice), "H2D"); }
    void get(T* dst, std::size_t count) { hip_check(hipMemcpy(dst, ptr, count * sizeof(T), hipMemcpyDeviceToHost), "D2H"); }
};

struct Module {
    hipModule_t module{};
    hipFunction_t function{};
    Module(const char* path, const char* symbol) {
        hip_check(hipModuleLoad(&module, path), "hipModuleLoad");
        hip_check(hipModuleGetFunction(&function, module, symbol), "hipModuleGetFunction");
    }
    ~Module() { if (module) hipModuleUnload(module); }
};

static lc::CellRecord pack(const CollapseState& c) {
    lc::CellRecord out{};
    out.current.rho = c.current.rho; out.current.T = c.current.T; out.current.e = c.current.e;
    for (int i = 0; i < 14; ++i) out.current.xn[i] = c.current.xn[i];
    out.time = c.time; out.density_driver = c.density_driver; out.completed_steps = c.completed_steps;
    out.stats.internal_steps = c.stats.internal_steps;
    out.stats.rhs_calls = c.stats.rhs_calls;
    out.stats.jacobian_calls = c.stats.jacobian_calls;
    out.stats.decompositions = c.stats.decompositions;
    out.stats.linear_solves = c.stats.linear_solves;
    out.stats.accepted_steps = c.stats.accepted_steps;
    out.stats.rejected_steps = c.stats.rejected_steps;
    return out;
}

struct Backend {
    bool loom;
    int n;
    int blocks;
    DeviceBuffer<CollapseState> original;
    DeviceBuffer<lc::CellRecord> cells;
    DeviceBuffer<double> candidates;
    DeviceBuffer<int> failure;
    DeviceBuffer<int> integrated;
    Module* prepare;
    Module* advance;
    Backend(bool use_loom, int count, Module* p, Module* a)
        : loom(use_loom), n(count), blocks((count + 127) / 128), original(count), cells(count),
          candidates(count), failure(1), integrated(1), prepare(p), advance(a) {}
    void reset(const std::vector<CollapseState>& init, const std::vector<lc::CellRecord>& packed) {
        if (loom) cells.put(packed.data(), n); else original.put(init.data(), n);
        int one = 1, zero = 0;
        failure.put(&one, 1); integrated.put(&zero, 1);
    }
    void launch_prepare(int completed, double time, int step, bool perturb) {
        if (!loom) {
            prepare_grid_timestep_kernel<<<blocks, 128>>>(original.ptr, n, completed, time, step,
                                                           perturb, candidates.ptr, failure.ptr);
            hip_check(hipGetLastError(), "original prepare launch");
        } else {
            void* args[] = {&cells.ptr, &n, &completed, &time, &step,
                            &perturb, &candidates.ptr, &failure.ptr};
            hip_check(hipModuleLaunchKernel(prepare->function, blocks, 1, 1, 128, 1, 1,
                                            0, nullptr, args, nullptr), "Loom prepare launch");
        }
    }
    void launch_advance(int completed, double next, double dt) {
        if (!loom) {
            advance_collapse_gridwide_kernel<<<blocks, 128>>>(original.ptr, n, completed, next, dt,
                                                               integrated.ptr, failure.ptr);
            hip_check(hipGetLastError(), "original advance launch");
        } else {
            void* args[] = {&cells.ptr, &n, &completed, &next, &dt,
                            &integrated.ptr, &failure.ptr};
            hip_check(hipModuleLaunchKernel(advance->function, blocks, 1, 1, 128, 1, 1,
                                            0, nullptr, args, nullptr), "Loom advance launch");
        }
    }
    int code() { int x; failure.get(&x, 1); return x; }
    int count() { int x; integrated.get(&x, 1); return x; }
    std::vector<double> dt() { std::vector<double> x(n); candidates.get(x.data(), n); return x; }
    std::vector<lc::CellRecord> state() {
        std::vector<lc::CellRecord> result(n);
        if (loom) cells.get(result.data(), n);
        else { std::vector<CollapseState> temp(n); original.get(temp.data(), n);
               for (int i = 0; i < n; ++i) result[i] = pack(temp[i]); }
        return result;
    }
};

static double rtol = 2e-4, atol = 1e-40;
static int mismatches = 0;
static void same(double a, double b, int cell, const char* field) {
    if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a - b) > atol + rtol * std::max(std::abs(a), std::abs(b))) {
        if (++mismatches <= 20) std::fprintf(stderr, "cell %d %s: original %.17g, Loom %.17g\n", cell, field, a, b);
    }
}
static void same_int(unsigned long long a, unsigned long long b, int cell, const char* field) {
    if (a != b && ++mismatches <= 20)
        std::fprintf(stderr, "cell %d %s: original %llu, Loom %llu\n", cell, field, a, b);
}
static void compare_state(const std::vector<lc::CellRecord>& a, const std::vector<lc::CellRecord>& b) {
    for (std::size_t i = 0; i < a.size(); ++i) {
        int cell = static_cast<int>(i);
        same(a[i].current.rho, b[i].current.rho, cell, "rho");
        same(a[i].current.T, b[i].current.T, cell, "T");
        same(a[i].current.e, b[i].current.e, cell, "e");
        for (int j = 0; j < 14; ++j) same(a[i].current.xn[j], b[i].current.xn[j], cell, "xn");
        same(a[i].time, b[i].time, cell, "time");
        same(a[i].density_driver, b[i].density_driver, cell, "density_driver");
        same_int(a[i].completed_steps, b[i].completed_steps, cell, "completed_steps");
#define CMP_STAT(x) same_int(a[i].stats.x, b[i].stats.x, cell, #x)
        CMP_STAT(internal_steps); CMP_STAT(rhs_calls); CMP_STAT(jacobian_calls);
        CMP_STAT(decompositions); CMP_STAT(linear_solves); CMP_STAT(accepted_steps); CMP_STAT(rejected_steps);
#undef CMP_STAT
    }
}

struct Event {
    hipEvent_t event{};
    Event() { hip_check(hipEventCreate(&event), "hipEventCreate"); }
    ~Event() { if (event) hipEventDestroy(event); }
};
static double timed(Backend& b, bool prep, double dt) {
    Event start, stop;
    hip_check(hipEventRecord(start.event), "event start");
    if (prep) b.launch_prepare(0, 0., 0, false);
    else b.launch_advance(0, dt, dt);
    hip_check(hipEventRecord(stop.event), "event stop");
    hip_check(hipEventSynchronize(stop.event), "event synchronize");
    float ms = 0;
    hip_check(hipEventElapsedTime(&ms, start.event, stop.event), "event elapsed");
    return ms;
}
static double timed_grid_step(Backend& b) {
    auto start = std::chrono::steady_clock::now();
    b.launch_prepare(0, 0., 0, true);
    hip_check(hipDeviceSynchronize(), "grid prepare synchronize");
    if (b.code() != 1) throw std::runtime_error("grid prepare failed");
    double dt = std::numeric_limits<double>::max();
    for (double candidate : b.dt()) dt = std::min(dt, candidate);
    if (dt == std::numeric_limits<double>::max()) throw std::runtime_error("grid has no timestep");
    b.launch_advance(0, dt, dt);
    hip_check(hipDeviceSynchronize(), "grid advance synchronize");
    if (b.code() != 1 || b.count() == 0) throw std::runtime_error("grid advance failed");
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
static double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

int main(int argc, char** argv) try {
    if (argc < 3) throw std::runtime_error("usage: compare_rocm PREPARE.hsaco ADVANCE.hsaco [--cells N] [--steps N] [--warmup N] [--repeats N] [--rtol X] [--atol X]");
    int n = 128, steps = 1, warmup = 1, repeats = 5;
    for (int i = 3; i < argc; i += 2) {
        if (i + 1 == argc) throw std::runtime_error("missing option value");
        std::string key = argv[i];
        if (key == "--cells") n = std::stoi(argv[i + 1]);
        else if (key == "--steps") steps = std::stoi(argv[i + 1]);
        else if (key == "--warmup") warmup = std::stoi(argv[i + 1]);
        else if (key == "--repeats") repeats = std::stoi(argv[i + 1]);
        else if (key == "--rtol") rtol = std::stod(argv[i + 1]);
        else if (key == "--atol") atol = std::stod(argv[i + 1]);
        else throw std::runtime_error("unknown option: " + key);
    }
    if (n < 1 || n > 1000000 || steps < 1 || steps > 1000 || warmup < 0 || repeats < 1 ||
        rtol < 0 || atol < 0) throw std::runtime_error("invalid option value");
    int devices = 0;
    hip_check(hipGetDeviceCount(&devices), "hipGetDeviceCount");
    if (!devices) throw std::runtime_error("no ROCm GPU visible");
    hipDeviceProp_t props{};
    hip_check(hipGetDeviceProperties(&props, 0), "hipGetDeviceProperties");
    if (std::string(props.gcnArchName).rfind("gfx942", 0) != 0)
        throw std::runtime_error(std::string("expected gfx942 GPU, got ") + props.gcnArchName);
    hip_check(hipSetDevice(0), "hipSetDevice");
    std::printf("GPU: %s (%s); cells=%d, correctness steps=%d, warmup=%d, repeats=%d\n",
                props.name, props.gcnArchName, n, steps, warmup, repeats);
    pc::set_redshift(30.0);
    std::vector<CollapseState> init(n);
    std::vector<lc::CellRecord> packed(n);
    for (int i = 0; i < n; ++i) {
        init[i] = make_collapse_state();
        init[i].current.T *= 1. + (i % 4) * .25;
        pc::eos_rt(init[i].current);
        packed[i] = pack(init[i]);
    }
    Module prepare(argv[1], "chemistry.prepare_grid_timestep_kernel");
    Module advance(argv[2], "chemistry.advance_collapse_gridwide_kernel");
    Backend original(false, n, nullptr, nullptr), loom(true, n, &prepare, &advance);
    original.reset(init, packed); loom.reset(init, packed);
    double original_time = 0., loom_time = 0., bench_dt = 0.;
    int completed = 0;
    for (int step = 0; step < steps; ++step) {
        original.launch_prepare(completed, original_time, step, true);
        loom.launch_prepare(completed, loom_time, step, true);
        hip_check(hipDeviceSynchronize(), "prepare synchronize");
        same_int(original.code(), loom.code(), -1, "prepare failure");
        auto dt_a = original.dt(), dt_b = loom.dt();
        double original_dt = std::numeric_limits<double>::max();
        double loom_dt = std::numeric_limits<double>::max();
        for (int i = 0; i < n; ++i) {
            same(dt_a[i], dt_b[i], i, "candidate");
            original_dt = std::min(original_dt, dt_a[i]);
            loom_dt = std::min(loom_dt, dt_b[i]);
        }
        same(original_dt, loom_dt, -1, "grid dt");
        compare_state(original.state(), loom.state());
        if (mismatches) break;
        if (original.code() != 1 || original_dt == std::numeric_limits<double>::max() ||
            loom_dt == std::numeric_limits<double>::max())
            throw std::runtime_error("grid stopped before requested correctness steps");
        if (step == 0) bench_dt = original_dt;
        double original_next = original_time + original_dt;
        double loom_next = loom_time + loom_dt;
        original.launch_advance(completed, original_next, original_dt);
        loom.launch_advance(completed, loom_next, loom_dt);
        hip_check(hipDeviceSynchronize(), "advance synchronize");
        same_int(original.code(), loom.code(), -1, "advance failure");
        same_int(original.count(), loom.count(), -1, "integrated count");
        compare_state(original.state(), loom.state());
        if (mismatches) break;
        if (original.code() != 1 || original.count() == 0)
            throw std::runtime_error("grid stopped before requested correctness steps");
        original_time = original_next; loom_time = loom_next; ++completed;
    }
    if (mismatches) { std::fprintf(stderr, "FAIL: %d mismatches (first 20 shown); performance skipped\n", mismatches); return 1; }
    std::printf("PASS: %d full gridwide steps; final grid time original %.17g, Loom %.17g; rtol=%g atol=%g\n",
                completed, original_time, loom_time, rtol, atol);
    for (bool prep : {true, false}) {
        std::vector<double> a, b;
        for (int rep = -warmup; rep < repeats; ++rep) {
            // Alternate order to limit drift from clock and temperature changes.
            Backend* order[] = {rep % 2 == 0 ? &original : &loom, rep % 2 == 0 ? &loom : &original};
            for (Backend* backend : order) {
                backend->reset(init, packed);
                double ms = timed(*backend, prep, bench_dt);
                if (backend->code() != 1 || (!prep && backend->count() == 0))
                    throw std::runtime_error("benchmark kernel failed or integrated no cells");
                if (rep >= 0) (backend->loom ? b : a).push_back(ms);
            }
        }
        double old_ms = median(a), loom_ms = median(b);
        std::printf("%s kernel GPU median: original %.6f ms, Loom %.6f ms, Loom/original %.3fx\n",
                    prep ? "prepare" : "advance", old_ms, loom_ms, loom_ms / old_ms);
    }
    std::vector<double> a, b;
    for (int rep = -warmup; rep < repeats; ++rep) {
        Backend* order[] = {rep % 2 == 0 ? &original : &loom, rep % 2 == 0 ? &loom : &original};
        for (Backend* backend : order) {
            backend->reset(init, packed);
            double ms = timed_grid_step(*backend);
            if (rep >= 0) (backend->loom ? b : a).push_back(ms);
        }
    }
    double old_ms = median(a), loom_ms = median(b);
    std::printf("full step median (launch, sync, candidate copy/min): original %.6f ms, Loom %.6f ms, Loom/original %.3fx\n",
                old_ms, loom_ms, loom_ms / old_ms);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "compare_rocm: %s\n", e.what());
    return 2;
}
