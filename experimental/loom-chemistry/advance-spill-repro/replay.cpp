// SPDX-License-Identifier: BSD-3-Clause
// Replays one saved grid launch on HIP and Loom from identical input state.
//
// Each sample resets both backends to the same snapshot, so every sample does
// the same solver work. Only the kernel is timed with HIP events; resets,
// copies and counter readback are outside the timed interval. The first
// (warmup) result of each backend is compared field-by-field with the
// tolerances of compare_rocm.cpp; any mismatch exits nonzero before timing.
//
// Build with hipcc from this directory (see run_gpu.sh). Defining
// REPLAY_HARNESS selects a different comparison harness, which is how the
// HIP-rewritten control replaces the original HIP kernels.
#ifndef REPLAY_HARNESS
#define REPLAY_HARNESS "../compare_rocm.cpp"
#endif
#ifndef REPLAY_HIP_LABEL
#define REPLAY_HIP_LABEL "HIP"
#endif
#define main comparison_main
#include REPLAY_HARNESS
#undef main

#include <fstream>
#include <sstream>

namespace {

struct Snapshot {
    std::vector<lc::CellRecord> cells;
    double time = 0., dt = 0.;
};

Snapshot load_snapshot(const std::string& dir, int step, bool advance) {
    std::string want = std::string(advance ? "advance" : "prepare") + "-step";
    char digits[8];
    std::snprintf(digits, sizeof digits, "%04d", step);
    want += digits; want += ".bin";
    std::ifstream csv(dir + "/snapshots.csv");
    if (!csv) throw std::runtime_error("cannot open " + dir + "/snapshots.csv");
    std::string line;
    std::getline(csv, line);
    Snapshot s;
    bool found = false;
    while (std::getline(csv, line)) {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        std::string file, time, dt;
        int row_step, phase;
        row >> file >> row_step >> phase >> time >> dt;
        if (file != want) continue;
        s.time = std::strtod(time.c_str(), nullptr);
        s.dt = std::strtod(dt.c_str(), nullptr);
        found = true;
        break;
    }
    if (!found) throw std::runtime_error("no snapshot " + want + " in " + dir);
    s.cells.resize(128);
    std::ifstream bin(dir + "/" + want, std::ios::binary | std::ios::ate);
    if (!bin || bin.tellg() != static_cast<std::streamoff>(s.cells.size() * sizeof(lc::CellRecord)))
        throw std::runtime_error(want + ": missing or wrong size");
    bin.seekg(0);
    bin.read(reinterpret_cast<char*>(s.cells.data()), s.cells.size() * sizeof(lc::CellRecord));
    return s;
}

CollapseState unpack(const lc::CellRecord& r) {
    CollapseState c{};
    c.current.rho = r.current.rho; c.current.T = r.current.T; c.current.e = r.current.e;
    for (int j = 0; j < 14; ++j) c.current.xn[j] = r.current.xn[j];
    c.time = r.time; c.density_driver = r.density_driver; c.completed_steps = r.completed_steps;
    c.stats.internal_steps = r.stats.internal_steps;
    c.stats.rhs_calls = r.stats.rhs_calls;
    c.stats.jacobian_calls = r.stats.jacobian_calls;
    c.stats.decompositions = r.stats.decompositions;
    c.stats.linear_solves = r.stats.linear_solves;
    c.stats.accepted_steps = r.stats.accepted_steps;
    c.stats.rejected_steps = r.stats.rejected_steps;
    return c;
}

}  // namespace

int main(int argc, char** argv) try {
    const char* usage =
        "usage: replay PREPARE.hsaco ADVANCE.hsaco SNAPSHOT_DIR --step N [--phase advance|prepare]\n"
        "              [--backend both|hip|loom] [--warmup N] [--repeats N] [--replicas N]";
    if (argc < 4) throw std::runtime_error(usage);
    int step = -1, warmup = 1, repeats = 5, replicas = 1, only = -1;
    bool advance_phase = true;
    for (int i = 4; i < argc; i += 2) {
        if (i + 1 == argc) throw std::runtime_error("missing option value\n" + std::string(usage));
        std::string key = argv[i], value = argv[i + 1];
        if (key == "--step") step = std::stoi(value);
        else if (key == "--phase") {
            if (value != "advance" && value != "prepare") throw std::runtime_error("--phase must be advance or prepare");
            advance_phase = value == "advance";
        } else if (key == "--backend") {
            if (value == "both") only = -1; else if (value == "hip") only = 0; else if (value == "loom") only = 1;
            else throw std::runtime_error("--backend must be both, hip, or loom");
        } else if (key == "--warmup") warmup = std::stoi(value);
        else if (key == "--repeats") repeats = std::stoi(value);
        else if (key == "--replicas") replicas = std::stoi(value);
        else throw std::runtime_error("unknown option: " + key + "\n" + usage);
    }
    if (step < 0 || warmup < 1 || repeats < 1 || replicas < 1 || replicas > 8192)
        throw std::runtime_error("invalid option value (warmup must be >= 1 for the correctness check)");

    hipDeviceProp_t props{};
    hip_check(hipGetDeviceProperties(&props, 0), "hipGetDeviceProperties");
    if (std::string(props.gcnArchName).rfind("gfx942", 0) != 0)
        throw std::runtime_error(std::string("expected gfx942 GPU, got ") + props.gcnArchName);
    hip_check(hipSetDevice(0), "device");
    pc::set_redshift(30.);

    Snapshot snap = load_snapshot(argv[3], step, advance_phase);
    // Replicas repeat the 128-cell snapshot across independent workgroups.
    // The Loom kernels must be compiled with workgroup_count.x == replicas.
    int n = 128 * replicas;
    std::vector<lc::CellRecord> packed(n);
    std::vector<CollapseState> initial(n);
    for (int i = 0; i < n; ++i) { packed[i] = snap.cells[i % 128]; initial[i] = unpack(packed[i]); }

    Module prepare(argv[1], "chemistry.prepare_grid_timestep_kernel");
    Module advance(argv[2], "chemistry.advance_collapse_gridwide_kernel");
    Backend hip(false, n, &prepare, &advance), loom(true, n, &prepare, &advance);
    Event start, stop;
    std::vector<lc::CellRecord> first[2];
    int first_count[2] = {}, first_code[2] = {};

    std::printf("backend,step,phase,cells,repeat,ms,status,integrated,internal_steps,rhs_calls,jacobians\n");
    // Negative repeats are warmups. Alternate backend order between samples.
    for (int rep = -warmup; rep < repeats; ++rep) for (int order = 0; order < 2; ++order) {
        int which = (order + (rep >= 0 ? rep : 0)) % 2;
        if (only >= 0 && which != only) continue;
        Backend& b = which ? loom : hip;
        b.reset(initial, packed);
        hip_check(hipDeviceSynchronize(), "reset sync");
        hip_check(hipEventRecord(start.event), "start");
        if (advance_phase) b.launch_advance(step, snap.time + snap.dt, snap.dt);
        else b.launch_prepare(step, snap.time, step, true);
        hip_check(hipEventRecord(stop.event), "stop");
        hip_check(hipEventSynchronize(stop.event), "wait");
        float ms;
        hip_check(hipEventElapsedTime(&ms, start.event, stop.event), "elapsed");
        auto out = b.state();
        unsigned long long ns = 0, nr = 0, nj = 0;
        for (int i = 0; i < n; ++i) {
            ns += out[i].stats.internal_steps - packed[i].stats.internal_steps;
            nr += out[i].stats.rhs_calls - packed[i].stats.rhs_calls;
            nj += out[i].stats.jacobian_calls - packed[i].stats.jacobian_calls;
        }
        std::printf("%s,%d,%d,%d,%d,%.9g,%d,%d,%llu,%llu,%llu\n", which ? "Loom" : REPLAY_HIP_LABEL,
                    step, advance_phase ? 1 : 0, n, rep, ms, b.code(), b.count(), ns, nr, nj);
        std::fflush(stdout);
        if (b.code() != 1) throw std::runtime_error("integration failed");
        if (rep == -warmup) { first[which] = std::move(out); first_count[which] = b.count(); first_code[which] = b.code(); }
        if (rep == -warmup && order == 1 && only < 0) {
            same_int(first_code[0], first_code[1], -1, "failure code");
            same_int(first_count[0], first_count[1], -1, "integrated count");
            compare_state(first[0], first[1]);
            if (mismatches) throw std::runtime_error("HIP and Loom results differ; timings are not comparable");
        }
    }
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "replay: %s\n", e.what());
    return 1;
}
