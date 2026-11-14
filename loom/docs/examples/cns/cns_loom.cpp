// Copyright 2026 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Host driver for the Loom port of AMReX's GPU CNS test (Sod shock tube,
// single level, no viscosity). It reproduces how AMReX submits the work: one
// hrx_stream_dispatch per box per kernel and boxes spread round-robin over the
// streams.
//
// Usage: cns_loom <cns.hsaco> [--n=64] [--bs=32] [--steps=20] [--streams=1]
//                 [--host-sync] [--graph]
//
// Phases of the step are ordered across streams with device-side event waits;
// the host waits only to read the time step and the totals. --host-sync
// instead synchronizes every stream from the host after each phase. The time
// reported per step covers the advance, the totals and the next time step, as
// AMReX's "Coarse TimeStep time" does.
//
// --graph records each RK2 step once as an HRX graph (one recorded command
// buffer with explicit dependencies between launches) and replays it every
// step. The time step and the totals are still computed with stream launches.
//
// With the defaults (AMReX's inputs-ci: 64^3 cells in 32^3 boxes) the totals
// after step 20 are compared with the CPU reference of AMReX's CNS test.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
#include <dirent.h>
#include <sched.h>
#endif

#include "hrx_runtime.h"

namespace {

[[noreturn]] void fail(const char* what, hrx_status_t status) {
  char* message = nullptr;
  size_t length = 0;
  if (status) hrx_status_to_string(status, &message, &length);
  std::fprintf(stderr, "cns_loom: %s failed: %s\n", what,
               message ? message : "(no status)");
  std::exit(1);
}

void check(hrx_status_t status, const char* what) {
  if (!hrx_status_is_ok(status)) fail(what, status);
}

// NUMA node of the CPU this thread runs on, or -1. HRX pins device threads to
// node 0 unless told otherwise, which fails on batch nodes without node-0 CPUs.
int current_numa_node() {
#if defined(__linux__)
  const int cpu = sched_getcpu();
  if (cpu < 0) return -1;
  char path[64];
  std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d", cpu);
  DIR* dir = opendir(path);
  if (!dir) return -1;
  int node = -1;
  while (const dirent* entry = readdir(dir)) {
    char tail = 0;
    if (std::sscanf(entry->d_name, "node%d%c", &node, &tail) == 1) break;
    node = -1;
  }
  closedir(dir);
  return node;
#else
  return -1;
#endif
}

// Problem parameters (CNS defaults and inputs-ci).
constexpr int kNcons = 7;
constexpr int kNprim = 8;
constexpr double kGamma = 1.4;
constexpr double kMu = 28.97;
constexpr double kSmallr = 1.e-19;
constexpr double kSmallp = 1.e-10;
constexpr double kCfl = 0.3;
constexpr double kStopTime = 0.2;
constexpr double kChangeMax = 1.1;
const double kCv = 8.31451e7 / (kMu * (kGamma - 1.0));

// Scalar parameters, packed in parameter order without padding, as Loom's
// constant block for hrx_stream_dispatch.
class Constants {
 public:
  template <class T>
  Constants& operator<<(T value) {
    const size_t offset = bytes_.size();
    bytes_.resize(offset + sizeof(T));
    std::memcpy(bytes_.data() + offset, &value, sizeof(T));
    return *this;
  }
  const uint8_t* data() const { return bytes_.data(); }
  size_t size() const { return bytes_.size(); }

 private:
  std::vector<uint8_t> bytes_;
};

struct Kernel {
  const char* name;
  uint32_t ordinal = 0;
  uint32_t constant_bytes = 0;
  uint32_t bindings = 0;
};

struct Options {
  const char* hsaco = nullptr;
  int n = 64;
  int bs = 32;
  int steps = 20;
  int streams = 1;
  bool host_sync = false;
  bool graph = false;
};

class Cns {
 public:
  explicit Cns(const Options& options)
      : options_(options),
        bs_(options.bs),
        boxes_per_side_(options.n / options.bs),
        boxes_(boxes_per_side_ * boxes_per_side_ * boxes_per_side_),
        dx_(1.0 / options.n) {
    if (options.n % options.bs) {
      std::fprintf(stderr, "cns_loom: --n must be a multiple of --bs\n");
      std::exit(1);
    }
    open_device();
    load_kernels();
    allocate();
    if (options_.graph) build_graphs();
  }

  ~Cns() {
    for (hrx_graph_exec_t exec : execs_) hrx_graph_exec_release(exec);
    for (hrx_graph_t graph : graphs_) hrx_graph_release(graph);
    for (hrx_event_t event : events_) hrx_event_release(event);
    for (hrx_buffer_t buffer : buffers_) hrx_buffer_release(buffer);
    for (hrx_stream_t stream : streams_) hrx_stream_release(stream);
    hrx_executable_release(executable_);
    hrx_device_group_release(group_);
    hrx_runtime_release(runtime_);
  }

  int run() {
    initialize();
    double time = 0.0;
    double dt = estimate_dt();
    if (time + dt > kStopTime - 0.001 * dt) dt = kStopTime - time;
    std::vector<double> totals;
    double steps_2_on = 0.0;
    for (int step = 1; step <= options_.steps; ++step) {
      std::printf("\n[Level 0 step %d] ADVANCE at time %.10g with dt = %.10g\n",
                  step, time, dt);
      const auto start = std::chrono::steady_clock::now();
      advance(dt);
      totals = compute_totals();
      const double next = estimate_dt();
      const double seconds =
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        start)
              .count();
      if (step > 1) steps_2_on += seconds;
      print_totals(totals);
      std::printf("[STEP %d] Coarse TimeStep time: %.9g\n", step, seconds);
      time += dt;
      dt = std::min(next, kChangeMax * dt);
      if (time + dt > kStopTime - 0.001 * dt) dt = kStopTime - time;
    }
    std::printf("\nsteps 2-%d: %.6f s total, %.3f ms per step\n",
                options_.steps, steps_2_on,
                1e3 * steps_2_on / std::max(1, options_.steps - 1));
    return check_reference(totals);
  }

 private:
  //===--------------------------------------------------------------------===//
  // Setup
  //===--------------------------------------------------------------------===//

  void open_device() {
    const hrx_host_allocator_t allocator = hrx_host_allocator_system();
    hrx_runtime_options_t runtime_options{};
    check(hrx_runtime_create(&runtime_options, allocator, &runtime_),
          "hrx_runtime_create");
    size_t count = 0;
    hrx_device_info_t* infos = nullptr;
    check(hrx_runtime_query_available_devices(runtime_, "amdgpu", allocator,
                                              &count, &infos),
          "hrx_runtime_query_available_devices");
    if (!count) {
      std::fprintf(stderr, "cns_loom: no amdgpu devices\n");
      std::exit(1);
    }
    std::string uri = infos[0].uri;
    if (const int node = current_numa_node(); node >= 0) {
      uri += uri.find('?') == std::string::npos ? '?' : '&';
      uri += "host_numa_node=" + std::to_string(node);
    }
    const hrx_device_create_params_t params = {uri.c_str(),
                                               HRX_WAIT_FLAG_YIELD};
    const hrx_status_t created =
        hrx_device_group_create(runtime_, 1, &params, &group_);
    hrx_host_allocator_free(allocator, infos);
    check(created, "hrx_device_group_create");
    device_ = hrx_device_group_get_device(group_, 0);

    constexpr hrx_queue_role_flags_t roles =
        HRX_QUEUE_ROLE_DISPATCH | HRX_QUEUE_ROLE_TRANSFER;
    uint32_t family = UINT32_MAX, provisioned = 0;
    const uint32_t families =
        static_cast<uint32_t>(hrx_device_queue_family_count(device_));
    for (uint32_t f = 0; f < families && family == UINT32_MAX; ++f) {
      hrx_queue_family_info_t info{};
      check(hrx_device_query_queue_family(device_, f, &info),
            "hrx_device_query_queue_family");
      if ((info.roles & roles) == roles && info.provisioned_queue_count) {
        family = f;
        provisioned = info.provisioned_queue_count;
      }
    }
    if (family == UINT32_MAX) {
      std::fprintf(stderr, "cns_loom: no dispatch and transfer queue family\n");
      std::exit(1);
    }
    // One stream per provisioned queue, as AMReX's LOOM runtime does.
    for (int s = 0; s < options_.streams; ++s) {
      hrx_stream_t stream = nullptr;
      check(hrx_stream_create(
                hrx_device_get_queue(device_, family,
                                     static_cast<uint32_t>(s) % provisioned),
                0, &stream),
            "hrx_stream_create");
      streams_.push_back(stream);
      hrx_event_t event = nullptr;
      check(hrx_event_create(device_, HRX_EVENT_FLAG_DISABLE_TIMING, &event),
            "hrx_event_create");
      events_.push_back(event);
    }
    std::printf("device %s, %d stream(s) on %u provisioned queue(s), %s\n",
                uri.c_str(), options_.streams, provisioned,
                options_.graph       ? "one graph per time step"
                : options_.host_sync ? "host synchronization between phases"
                                     : "device-side ordering between phases");
  }

  void load_kernels() {
    check(hrx_executable_load_file(device_, options_.hsaco, "amdgpu", "gfx90a",
                                   &executable_),
          "hrx_executable_load_file");
    for (Kernel* kernel :
         {&init_, &temp_, &fill_, &ctoprim_, &slope_[0], &slope_[1],
          &slope_[2], &riemann_[0], &riemann_[1], &riemann_[2], &dudt_,
          &rk1_, &rk2_, &estdt_, &sums_}) {
      check(hrx_executable_lookup_export_by_name(executable_, kernel->name,
                                                 &kernel->ordinal),
            kernel->name);
      hrx_executable_export_info_t info{};
      check(hrx_executable_export_info(executable_, kernel->ordinal, &info),
            "hrx_executable_export_info");
      kernel->constant_bytes = info.constant_byte_length;
      kernel->bindings = info.binding_count;
    }
  }

  hrx_buffer_t allocate_buffer(size_t elements) {
    hrx_buffer_t buffer = nullptr;
    check(hrx_buffer_allocate(streams_[0], elements * sizeof(double),
                              HRX_MEMORY_TYPE_DEVICE_LOCAL,
                              HRX_BUFFER_USAGE_DEFAULT, &buffer),
          "hrx_buffer_allocate");
    buffers_.push_back(buffer);
    return buffer;
  }

  void allocate() {
    const size_t sn = bs_ + 4, dn = bs_ + 2;
    state_box_ = sn * sn * sn * kNcons;
    dsdt_box_ = static_cast<size_t>(bs_) * bs_ * bs_ * kNcons;
    flux_box_ = static_cast<size_t>(bs_ + 1) * bs_ * bs_ * kNcons;
    prim_box_ = sn * sn * sn * kNprim;
    slope_box_ = dn * dn * dn * 5;
    old_ = allocate_buffer(state_box_ * boxes_);
    new_ = allocate_buffer(state_box_ * boxes_);
    dsdt_ = allocate_buffer(dsdt_box_ * boxes_);
    for (int d = 0; d < 3; ++d) flux_[d] = allocate_buffer(flux_box_ * boxes_);
    // Primitive and slope temporaries, one set per stream (AMReX resizes one
    // FArrayBox per MFIter iteration; boxes on one stream run in order). A
    // graph may run boxes concurrently, so it gets one set per box.
    const int temporaries = options_.graph ? boxes_ : options_.streams;
    for (int s = 0; s < temporaries; ++s) {
      prim_.push_back(allocate_buffer(prim_box_));
      slope_tmp_.push_back(allocate_buffer(slope_box_));
    }
    const size_t groups =
        (static_cast<size_t>(boxes_) * bs_ * bs_ * bs_ + 255) / 256;
    partial_ = allocate_buffer(groups);
    partial_host_.resize(groups);
    sums_partial_ = allocate_buffer(5 * groups);
    time_step_ = allocate_buffer(1);
    // The initial data goes to new_; the first step swaps it into old_.
    state_a_ = new_;
    state_b_ = old_;
    sums_host_.resize(5 * groups);
  }

  //===--------------------------------------------------------------------===//
  // Launches
  //===--------------------------------------------------------------------===//

  hrx_buffer_ref_t box_ref(hrx_buffer_t buffer, size_t box_elements, int box) {
    return {buffer, box * box_elements * sizeof(double),
            box_elements * sizeof(double)};
  }
  hrx_buffer_ref_t whole_ref(hrx_buffer_t buffer, size_t elements) {
    return {buffer, 0, elements * sizeof(double)};
  }

  void dispatch(hrx_stream_t stream, const Kernel& kernel, uint32_t elements,
                const Constants& constants,
                std::initializer_list<hrx_buffer_ref_t> bindings) {
    if (constants.size() != kernel.constant_bytes ||
        bindings.size() != kernel.bindings) {
      std::fprintf(stderr,
                   "cns_loom: %s expects %u constant bytes and %u bindings, "
                   "got %zu and %zu\n",
                   kernel.name, kernel.constant_bytes, kernel.bindings,
                   constants.size(), bindings.size());
      std::exit(1);
    }
    const hrx_dispatch_config_t config = {
        {(elements + 255) / 256, 1, 1}, {256, 1, 1}, 0};
    check(hrx_stream_dispatch(stream, executable_, kernel.ordinal, &config,
                              constants.data(), constants.size(),
                              bindings.begin(), bindings.size(),
                              HRX_DISPATCH_FLAG_NONE),
          kernel.name);
  }

  hrx_stream_t stream_for(int box) { return streams_[box % streams_.size()]; }

  void synchronize() {
    for (hrx_stream_t stream : streams_) {
      check(hrx_stream_synchronize(stream), "hrx_stream_synchronize");
    }
  }

  // Orders the next phase after all work of this phase on every stream. AMReX
  // synchronizes the host at the end of each MFIter loop; device-side waits
  // give the same ordering without returning to the host.
  void phase_barrier() {
    if (options_.host_sync) {
      synchronize();
      return;
    }
    if (streams_.size() == 1) return;
    for (size_t s = 0; s < streams_.size(); ++s) {
      check(hrx_event_record(events_[s], streams_[s]), "hrx_event_record");
    }
    for (size_t s = 0; s < streams_.size(); ++s) {
      for (size_t t = 0; t < streams_.size(); ++t) {
        if (s != t) {
          check(hrx_stream_wait_event(streams_[s], events_[t]),
                "hrx_stream_wait_event");
        }
      }
    }
  }

  //===--------------------------------------------------------------------===//
  // Time step
  //===--------------------------------------------------------------------===//

  // CNS::initData followed by computeTemp.
  void initialize() {
    const uint32_t cells = bs_ * bs_ * bs_;
    for (int b = 0; b < boxes_; ++b) {
      const int lox = (b % boxes_per_side_) * bs_;
      dispatch(stream_for(b), init_, cells,
               Constants() << cells << bs_ << lox << dx_ << kGamma << 1.0
                           << 0.1 << 1.0 << 0.125 << 0.0 << 0.0,
               {box_ref(new_, state_box_, b)});
    }
    phase_barrier();
    compute_temp(new_);
  }

  void compute_temp(hrx_buffer_t state) {
    const uint32_t cells = bs_ * bs_ * bs_;
    for (int b = 0; b < boxes_; ++b) {
      dispatch(stream_for(b), temp_, cells, Constants() << cells << bs_ << kCv,
               {box_ref(state, state_box_, b)});
    }
    phase_barrier();
  }

  // FillPatch: neighbor boxes, periodic y/z, foextrap x.
  void fill_ghosts(hrx_buffer_t state) {
    const uint32_t cells = (bs_ + 4) * (bs_ + 4) * (bs_ + 4);
    for (int b = 0; b < boxes_; ++b) {
      dispatch(stream_for(b), fill_, cells,
               Constants() << cells << bs_ << boxes_per_side_ << b,
               {whole_ref(state, state_box_ * boxes_)});
    }
    phase_barrier();
  }

  // CNS::compute_dSdt: the per-box kernel chain.
  void compute_dsdt(hrx_buffer_t state) {
    const int s1 = bs_ + 1;
    for (int b = 0; b < boxes_; ++b) {
      hrx_stream_t stream = stream_for(b);
      const size_t si = b % streams_.size();
      const hrx_buffer_ref_t q = whole_ref(prim_[si], prim_box_);
      const hrx_buffer_ref_t dq = whole_ref(slope_tmp_[si], slope_box_);
      const uint32_t grown2 = (bs_ + 4) * (bs_ + 4) * (bs_ + 4);
      dispatch(stream, ctoprim_, grown2,
               Constants() << grown2 << bs_ << kGamma << kSmallr << kSmallp
                           << kCv,
               {box_ref(state, state_box_, b), q});
      for (int d = 0; d < 3; ++d) {
        const uint32_t slope_cells = (bs_ + 2) * bs_ * bs_;
        dispatch(stream, slope_[d], slope_cells,
                 Constants() << slope_cells << bs_, {q, dq});
        const uint32_t faces = s1 * bs_ * bs_;
        dispatch(stream, riemann_[d], faces,
                 Constants() << faces << bs_ << kGamma << kSmallp << kSmallr,
                 {dq, q, box_ref(flux_[d], flux_box_, b)});
      }
      const uint32_t updates = bs_ * bs_ * bs_ * kNcons;
      const double dxinv = 1.0 / dx_;
      dispatch(stream, dudt_, updates,
               Constants() << updates << bs_ << dxinv << dxinv << dxinv,
               {box_ref(flux_[0], flux_box_, b),
                box_ref(flux_[1], flux_box_, b),
                box_ref(flux_[2], flux_box_, b),
                box_ref(dsdt_, dsdt_box_, b)});
    }
    phase_barrier();
  }

  void rk_update(const Kernel& kernel) {
    const uint32_t elements = boxes_ * bs_ * bs_ * bs_ * kNcons;
    dispatch(streams_[0], kernel, elements,
             Constants() << elements << bs_,
             {whole_ref(time_step_, 1), whole_ref(old_, state_box_ * boxes_),
              whole_ref(dsdt_, dsdt_box_ * boxes_),
              whole_ref(new_, state_box_ * boxes_)});
    phase_barrier();
  }

  // CNS::advance with RungeKutta::RK2.
  void advance(double dt) {
    std::swap(old_, new_);  // StateData::swapTimeLevels
    check(hrx_stream_update_buffer(streams_[0], &dt, sizeof(dt), time_step_, 0),
          "hrx_stream_update_buffer");
    if (options_.graph) {
      check(hrx_graph_exec_launch(execs_[old_ == state_a_ ? 0 : 1],
                                  streams_[0]),
            "hrx_graph_exec_launch");
      return;
    }
    fill_ghosts(old_);
    compute_dsdt(old_);
    rk_update(rk1_);
    compute_temp(new_);
    fill_ghosts(new_);
    compute_dsdt(new_);
    rk_update(rk2_);
    compute_temp(new_);
  }

  //===--------------------------------------------------------------------===//
  // Graph mode
  //===--------------------------------------------------------------------===//

  hrx_graph_node_t add_node(hrx_graph_t graph, const Kernel& kernel,
                            uint32_t elements, const Constants& constants,
                            std::initializer_list<hrx_buffer_ref_t> bindings,
                            const std::vector<hrx_graph_node_t>& deps) {
    if (constants.size() != kernel.constant_bytes ||
        bindings.size() != kernel.bindings) {
      std::fprintf(stderr, "cns_loom: %s argument mismatch\n", kernel.name);
      std::exit(1);
    }
    hrx_graph_kernel_node_attrs_t attrs{};
    attrs.executable = executable_;
    attrs.export_ordinal = kernel.ordinal;
    attrs.config = {{(elements + 255) / 256, 1, 1}, {256, 1, 1}, 0};
    attrs.constants = constants.data();
    attrs.constants_size = constants.size();
    attrs.bindings = bindings.begin();
    attrs.binding_count = bindings.size();
    attrs.flags = HRX_DISPATCH_FLAG_NONE;
    hrx_graph_node_t node = nullptr;
    check(hrx_graph_add_kernel_node(graph, deps.data(), deps.size(), &attrs,
                                    &node),
          kernel.name);
    return node;
  }

  hrx_graph_node_t add_join(hrx_graph_t graph,
                            const std::vector<hrx_graph_node_t>& deps) {
    hrx_graph_node_t node = nullptr;
    check(hrx_graph_add_empty_node(graph, deps.data(), deps.size(), &node),
          "hrx_graph_add_empty_node");
    return node;
  }

  // Ghost fill, then each box's compute_dSdt chain after its own fill. A box's
  // fill writes only its own ghost cells, so chains need not wait for others.
  std::vector<hrx_graph_node_t> add_stage(hrx_graph_t graph,
                                          hrx_buffer_t state,
                                          hrx_graph_node_t after) {
    const std::vector<hrx_graph_node_t> start =
        after ? std::vector<hrx_graph_node_t>{after}
              : std::vector<hrx_graph_node_t>{};
    const uint32_t grown2 = (bs_ + 4) * (bs_ + 4) * (bs_ + 4);
    const uint32_t slope_cells = (bs_ + 2) * bs_ * bs_;
    const uint32_t faces = (bs_ + 1) * bs_ * bs_;
    const uint32_t updates = bs_ * bs_ * bs_ * kNcons;
    const double dxinv = 1.0 / dx_;
    std::vector<hrx_graph_node_t> done;
    for (int b = 0; b < boxes_; ++b) {
      hrx_graph_node_t node =
          add_node(graph, fill_, grown2,
                   Constants() << grown2 << bs_ << boxes_per_side_ << b,
                   {whole_ref(state, state_box_ * boxes_)}, start);
      const hrx_buffer_ref_t q = whole_ref(prim_[b], prim_box_);
      const hrx_buffer_ref_t dq = whole_ref(slope_tmp_[b], slope_box_);
      node = add_node(graph, ctoprim_, grown2,
                      Constants() << grown2 << bs_ << kGamma << kSmallr
                                  << kSmallp << kCv,
                      {box_ref(state, state_box_, b), q}, {node});
      for (int d = 0; d < 3; ++d) {
        node = add_node(graph, slope_[d], slope_cells,
                        Constants() << slope_cells << bs_, {q, dq}, {node});
        node = add_node(graph, riemann_[d], faces,
                        Constants() << faces << bs_ << kGamma << kSmallp
                                    << kSmallr,
                        {dq, q, box_ref(flux_[d], flux_box_, b)}, {node});
      }
      node = add_node(graph, dudt_, updates,
                      Constants() << updates << bs_ << dxinv << dxinv << dxinv,
                      {box_ref(flux_[0], flux_box_, b),
                       box_ref(flux_[1], flux_box_, b),
                       box_ref(flux_[2], flux_box_, b),
                       box_ref(dsdt_, dsdt_box_, b)},
                      {node});
      done.push_back(node);
    }
    return done;
  }

  // RK update over all boxes, then each box's temperature; returns the join.
  hrx_graph_node_t add_update(hrx_graph_t graph, const Kernel& kernel,
                              hrx_buffer_t old_state, hrx_buffer_t new_state,
                              const std::vector<hrx_graph_node_t>& after) {
    const uint32_t elements = boxes_ * bs_ * bs_ * bs_ * kNcons;
    const uint32_t cells = bs_ * bs_ * bs_;
    hrx_graph_node_t update = add_node(
        graph, kernel, elements, Constants() << elements << bs_,
        {whole_ref(time_step_, 1), whole_ref(old_state, state_box_ * boxes_),
         whole_ref(dsdt_, dsdt_box_ * boxes_),
         whole_ref(new_state, state_box_ * boxes_)},
        {add_join(graph, after)});
    std::vector<hrx_graph_node_t> temps;
    for (int b = 0; b < boxes_; ++b) {
      temps.push_back(add_node(graph, temp_, cells,
                               Constants() << cells << bs_ << kCv,
                               {box_ref(new_state, state_box_, b)}, {update}));
    }
    return add_join(graph, temps);
  }

  // One RK2 step from old_state to new_state, recorded once and replayed.
  void build_graph(int index, hrx_buffer_t old_state, hrx_buffer_t new_state) {
    hrx_graph_t graph = nullptr;
    check(hrx_graph_create(device_, 0, &graph), "hrx_graph_create");
    const auto stage1 = add_stage(graph, old_state, nullptr);
    const hrx_graph_node_t mid =
        add_update(graph, rk1_, old_state, new_state, stage1);
    const auto stage2 = add_stage(graph, new_state, mid);
    add_update(graph, rk2_, old_state, new_state, stage2);
    check(hrx_graph_instantiate(graph, hrx_stream_get_queue(streams_[0]), 0,
                                &execs_[index]),
          "hrx_graph_instantiate");
    graphs_[index] = graph;
  }

  void build_graphs() {
    const auto start = std::chrono::steady_clock::now();
    build_graph(0, state_a_, state_b_);
    build_graph(1, state_b_, state_a_);
    std::printf("graphs recorded and instantiated in %.3f s\n",
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start)
                    .count());
  }

  // CNS::estTimeStep: the minimum over cells, times the CFL number.
  double estimate_dt() {
    const uint32_t cells = boxes_ * bs_ * bs_ * bs_;
    dispatch(streams_[0], estdt_, cells,
             Constants() << cells << bs_ << dx_ << kGamma << kSmallr
                         << kSmallp,
             {whole_ref(new_, state_box_ * boxes_),
              whole_ref(partial_, partial_host_.size())});
    check(hrx_stream_copy_d2h(streams_[0], partial_, 0, partial_host_.data(),
                              partial_host_.size() * sizeof(double)),
          "hrx_stream_copy_d2h");
    // Stream 0 is ordered after every stream's earlier phases.
    check(hrx_stream_synchronize(streams_[0]), "hrx_stream_synchronize");
    return kCfl *
           *std::min_element(partial_host_.begin(), partial_host_.end());
  }

  // CNS::printTotal: sums of the first five components over valid cells,
  // reduced per workgroup on the device and finished on the host.
  std::vector<double> compute_totals() {
    const uint32_t cells = boxes_ * bs_ * bs_ * bs_;
    const int groups = static_cast<int>(partial_host_.size());
    dispatch(streams_[0], sums_, cells, Constants() << cells << bs_ << groups,
             {whole_ref(new_, state_box_ * boxes_),
              whole_ref(sums_partial_, sums_host_.size())});
    check(hrx_stream_copy_d2h(streams_[0], sums_partial_, 0, sums_host_.data(),
                              sums_host_.size() * sizeof(double)),
          "hrx_stream_copy_d2h");
    check(hrx_stream_synchronize(streams_[0]), "hrx_stream_synchronize");
    std::vector<double> totals(5, 0.0);
    for (int c = 0; c < 5; ++c) {
      for (int g = 0; g < groups; ++g) totals[c] += sums_host_[c * groups + g];
    }
    return totals;
  }

  void print_totals(const std::vector<double>& totals) {
    std::printf(
        "\n[CNS] Total mass       is %.17g\n      Total x-momentum is %.17g\n"
        "      Total y-momentum is %.17g\n      Total z-momentum is %.17g\n"
        "      Total energy     is %.17g\n",
        totals[0], totals[1], totals[2], totals[3], totals[4]);
  }

  // AMReX's CNS test checks the inputs-ci configuration against a CPU run.
  int check_reference(const std::vector<double>& totals) {
    if (options_.n != 64 || options_.bs != 32 || options_.steps != 20) {
      std::printf("no reference for this configuration\n");
      return 0;
    }
    const double reference[] = {147455.99999999956, 11281.216843665323,
                                360448.00000000076};
    const int components[] = {0, 1, 4};
    const char* names[] = {"mass", "xmom", "energy"};
    double worst = 0.0;
    for (int i = 0; i < 3; ++i) {
      const double relative =
          std::fabs(totals[components[i]] - reference[i]) /
          std::fabs(reference[i]);
      worst = std::max(worst, relative);
      std::printf("%s: %.17g (CPU reference %.17g, relative %.1e)\n", names[i],
                  totals[components[i]], reference[i], relative);
    }
    const bool pass = worst < 1e-12;
    std::printf("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
  }

  Options options_;
  int bs_, boxes_per_side_, boxes_;
  double dx_;
  hrx_runtime_t runtime_ = nullptr;
  hrx_device_group_t group_ = nullptr;
  hrx_device_t device_ = nullptr;
  std::vector<hrx_stream_t> streams_;
  std::vector<hrx_event_t> events_;
  hrx_executable_t executable_ = nullptr;
  Kernel init_{"cns_init"}, temp_{"cns_compute_temp"}, fill_{"cns_fill_ghosts"},
      ctoprim_{"cns_ctoprim"}, dudt_{"cns_flux_to_dudt"},
      rk1_{"cns_rk_update"}, rk2_{"cns_rk2_update_2"}, estdt_{"cns_estdt"},
      sums_{"cns_sums"};
  Kernel slope_[3] = {{"cns_slope_x"}, {"cns_slope_y"}, {"cns_slope_z"}};
  Kernel riemann_[3] = {
      {"cns_riemann_x"}, {"cns_riemann_y"}, {"cns_riemann_z"}};
  size_t state_box_ = 0, dsdt_box_ = 0, flux_box_ = 0, prim_box_ = 0,
         slope_box_ = 0;
  std::vector<hrx_buffer_t> buffers_;
  hrx_buffer_t old_ = nullptr, new_ = nullptr, dsdt_ = nullptr;
  hrx_buffer_t flux_[3] = {};
  std::vector<hrx_buffer_t> prim_, slope_tmp_;
  hrx_buffer_t partial_ = nullptr;
  std::vector<double> partial_host_;
  hrx_buffer_t sums_partial_ = nullptr;
  std::vector<double> sums_host_;
  hrx_buffer_t time_step_ = nullptr;
  hrx_buffer_t state_a_ = nullptr, state_b_ = nullptr;
  hrx_graph_t graphs_[2] = {};
  hrx_graph_exec_t execs_[2] = {};
};

}  // namespace

int main(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strncmp(arg, "--n=", 4) == 0) {
      options.n = std::atoi(arg + 4);
    } else if (std::strncmp(arg, "--bs=", 5) == 0) {
      options.bs = std::atoi(arg + 5);
    } else if (std::strncmp(arg, "--steps=", 8) == 0) {
      options.steps = std::atoi(arg + 8);
    } else if (std::strncmp(arg, "--streams=", 10) == 0) {
      options.streams = std::max(1, std::atoi(arg + 10));
    } else if (std::strcmp(arg, "--host-sync") == 0) {
      options.host_sync = true;
    } else if (std::strcmp(arg, "--graph") == 0) {
      options.graph = true;
    } else if (!options.hsaco && arg[0] != '-') {
      options.hsaco = arg;
    } else {
      std::fprintf(stderr, "cns_loom: unknown argument %s\n", arg);
      return 2;
    }
  }
  if (!options.hsaco) {
    std::fprintf(stderr,
                 "usage: cns_loom <cns.hsaco> [--n=64] [--bs=32] [--steps=20] "
                 "[--streams=1] [--host-sync] [--graph]\n");
    return 2;
  }
  Cns cns(options);
  return cns.run();
}
