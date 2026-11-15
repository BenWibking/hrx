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
//                 [--host-sync | --host-sync=flush] [--immediate] [--graph]
//                 [--fillpatch=amrex] [--phase-times]
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
#include <limits>
#include <map>
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
  bool flush_first = false;  // --host-sync=flush: submit every stream, then wait
  bool immediate = false;    // --immediate: AMReX LOOM's per-launch queue dispatch
  bool graph = false;
  bool phase_times = false;
  bool fillpatch_amrex = false;  // --fillpatch=amrex: AMReX's FillPatch
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
    if (options_.fillpatch_amrex) fp_setup();
    if (options_.graph) build_graphs();
  }

  ~Cns() {
    for (hrx_graph_exec_t exec : execs_) hrx_graph_exec_release(exec);
    for (hrx_graph_t graph : graphs_) hrx_graph_release(graph);
    for (hrx_event_t event : events_) hrx_event_release(event);
    for (const Pinned& p : pinned_) {
      hrx_status_ignore(hrx_buffer_unmap(p.buffer));
      hrx_buffer_release(p.buffer);
    }
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
      phase_start_ = start;
      timing_ = true;
      advance(dt);
      totals = compute_totals();
      phase_done("totals");
      const double next = estimate_dt();
      phase_done("estimate_dt");
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
    for (const auto& [name, entry] : phase_times_) {
      std::printf("phase %-14s %5d calls %10.6f s %9.3f us/call\n", name.c_str(),
                  entry.first, entry.second, 1e6 * entry.second / entry.first);
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
    if (options_.graph && options_.immediate) {
      std::fprintf(stderr, "cns_loom: --graph and --immediate are exclusive\n");
      std::exit(2);
    }
    if (options_.immediate) std::printf("immediate queue dispatches\n");
    if (options_.graph && options_.fillpatch_amrex) {
      std::fprintf(stderr, "cns_loom: --graph and --fillpatch=amrex are exclusive\n");
      std::exit(2);
    }
  }

  void load_kernels() {
    check(hrx_executable_load_file(device_, options_.hsaco, "amdgpu", "gfx90a",
                                   &executable_),
          "hrx_executable_load_file");
    for (Kernel* kernel :
         {&init_, &temp_, &fill_, &ctoprim_, &slope_[0], &slope_[1],
          &slope_[2], &riemann_[0], &riemann_[1], &riemann_[2], &dudt_,
          &rk1_, &rk2_, &estdt_, &sums_, &fp_setc_, &fp_tag_, &fp_bc_,
          &fp_slab_, &fp_mfcopy_}) {
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
    if (options_.immediate) {
      // As AMReX LOOM's dispatch_immediate: flush recorded work, then one
      // hrx_queue_dispatch chained on the stream's timeline, no barriers.
      check(hrx_stream_flush(stream), "hrx_stream_flush");
      hrx_timeline_point_t after{};
      check(hrx_stream_get_timeline_position(stream, &after),
            "hrx_stream_get_timeline_position");
      hrx_semaphore_t timeline = nullptr;
      check(hrx_stream_get_semaphore(stream, &timeline), "hrx_stream_get_semaphore");
      uint64_t done = 0;
      check(hrx_stream_advance_timeline(stream, &done), "hrx_stream_advance_timeline");
      hrx_semaphore_t wait_semaphore = after.semaphore;
      uint64_t wait_value = after.value;
      const hrx_semaphore_list_t waits{&wait_semaphore, &wait_value,
                                       after.value ? 1u : 0u};
      const hrx_semaphore_list_t signals{&timeline, &done, 1};
      static const hrx_barrier_list_t no_barriers{0, nullptr};
      static const hrx_queue_barriers_t barriers{&no_barriers, &no_barriers};
      unreleased_[stream] = true;
      check(hrx_queue_dispatch(hrx_stream_get_queue(stream), &waits, &signals,
                               executable_, kernel.ordinal, &config,
                               constants.data(), constants.size(),
                               bindings.begin(), bindings.size(), &barriers,
                               HRX_DISPATCH_FLAG_NONE),
            kernel.name);
      return;
    }
    check(hrx_stream_dispatch(stream, executable_, kernel.ordinal, &config,
                              constants.data(), constants.size(),
                              bindings.begin(), bindings.size(),
                              HRX_DISPATCH_FLAG_NONE),
          kernel.name);
  }

  hrx_stream_t stream_for(int box) { return streams_[box % streams_.size()]; }

  // As AMReX LOOM's flush_all: an empty recording after immediate dispatches
  // (system-scope release), then submit every stream before waiting on any.
  void flush_all() {
    for (auto& [stream, pending] : unreleased_) {
      if (pending) check(hrx_stream_barrier(stream, nullptr), "hrx_stream_barrier");
    }
    unreleased_.clear();
    for (hrx_stream_t stream : streams_) {
      check(hrx_stream_flush(stream), "hrx_stream_flush");
    }
  }

  void synchronize() {
    if (options_.flush_first || options_.immediate) flush_all();
    for (hrx_stream_t stream : streams_) {
      check(hrx_stream_synchronize(stream), "hrx_stream_synchronize");
    }
  }

  // Orders the next phase after all work of this phase on every stream. AMReX
  // synchronizes the host at the end of each MFIter loop; device-side waits
  // give the same ordering without returning to the host.
  void phase_barrier(const char* name = "other") {
    if (options_.host_sync) {
      synchronize();
      phase_done(name);
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
    phase_barrier("computeTemp");
  }

  // FillPatch: neighbor boxes, periodic y/z, foextrap x.
  void fill_ghosts(hrx_buffer_t state) {
    const uint32_t cells = (bs_ + 4) * (bs_ + 4) * (bs_ + 4);
    for (int b = 0; b < boxes_; ++b) {
      dispatch(stream_for(b), fill_, cells,
               Constants() << cells << bs_ << boxes_per_side_ << b,
               {whole_ref(state, state_box_ * boxes_)});
    }
    phase_barrier("fill_ghosts");
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
    phase_barrier("compute_dSdt");
  }

  void rk_update(const Kernel& kernel) {
    const uint32_t elements = boxes_ * bs_ * bs_ * bs_ * kNcons;
    dispatch(streams_[0], kernel, elements,
             Constants() << elements << bs_,
             {whole_ref(time_step_, 1), whole_ref(old_, state_box_ * boxes_),
              whole_ref(dsdt_, dsdt_box_ * boxes_),
              whole_ref(new_, state_box_ * boxes_)});
    phase_barrier("rk_update");
  }

  // CNS::advance with RungeKutta::RK2.
  void advance(double dt) {
    std::swap(old_, new_);  // StateData::swapTimeLevels
    check(hrx_stream_update_buffer_linear(streams_[0], &dt, sizeof(dt), time_step_, 0),
          "hrx_stream_update_buffer_linear");
    if (options_.graph) {
      check(hrx_graph_exec_launch(execs_[old_ == state_a_ ? 0 : 1],
                                  streams_[0]),
            "hrx_graph_exec_launch");
      return;
    }
    fill_patch(old_);
    compute_dsdt(old_);
    rk_update(rk1_);
    compute_temp(new_);
    fill_patch(new_);
    compute_dsdt(new_);
    rk_update(rk2_);
    compute_temp(new_);
  }


  //===--------------------------------------------------------------------===//
  // AMReX FillPatch (--fillpatch=amrex)
  //
  // AmrLevel::FillPatch for one level, as AMReX runs it on LOOM: a temporary
  // MultiFab with two ghost cells; FabArray::setDomainBndry(NaN) in an MFIter
  // loop; FillPatchSingleLevel's periodic ParallelCopy from the state (a tag
  // table copied from pinned memory, one fused kernel, a stream sync when the
  // TagVector goes away); StateDataPhysBCFunct in an MFIter loop (per
  // boundary box: a BCRec array copied from pinned memory and one face kernel,
  // and the periodic corner fix-up through a temporary slab); then
  // MultiFab::Copy back into the state (the source's Array4 table copied from
  // pinned memory, its LOOM offset table copied synchronously from pageable
  // memory, one fused kernel, a stream sync). Arena allocations become
  // preallocated slots, reused only after a host synchronization, as AMReX's
  // CArena reuses memory.
  //===--------------------------------------------------------------------===//

  struct Box3 {
    int lo[3], hi[3];
    bool ok() const { return lo[0] <= hi[0] && lo[1] <= hi[1] && lo[2] <= hi[2]; }
    int len(int d) const { return hi[d] - lo[d] + 1; }
    long npts() const { return ok() ? long(len(0)) * len(1) * len(2) : 0; }
    bool contains(const Box3& b) const {
      for (int d = 0; d < 3; ++d) {
        if (b.lo[d] < lo[d] || b.hi[d] > hi[d]) return false;
      }
      return true;
    }
    Box3 operator&(const Box3& b) const {
      Box3 r;
      for (int d = 0; d < 3; ++d) {
        r.lo[d] = std::max(lo[d], b.lo[d]);
        r.hi[d] = std::min(hi[d], b.hi[d]);
      }
      return r;
    }
    Box3 shifted(int d, int by) const {
      Box3 r = *this;
      r.lo[d] += by;
      r.hi[d] += by;
      return r;
    }
    Box3 grown(int g) const {
      Box3 r = *this;
      for (int d = 0; d < 3; ++d) {
        r.lo[d] -= g;
        r.hi[d] += g;
      }
      return r;
    }
  };

  // A fab: a buffer range holding a box of cells, all NCONS components.
  struct Fab {
    hrx_buffer_t buffer;
    size_t offset;  // bytes
    Box3 box;
    hrx_buffer_ref_t ref() const {
      return {buffer, offset, size_t(box.npts()) * kNcons * sizeof(double)};
    }
  };

  struct Pinned {
    hrx_buffer_t buffer = nullptr;
    uint8_t* host = nullptr;
  };

  static constexpr int kTagInts = 24, kEntInts = 14, kBoxInts = 8;
  static constexpr int kWarp = 64, kBlock = 256;
  static constexpr int kBcrInts = 6 * kNcons;
  static constexpr int kArray4Bytes = 64;  // sizeof(amrex::Array4<double>)

  bool periodic(int d) const { return d != 0; }  // CNS Sod: periodic in y, z
  Box3 domain() const { return {{0, 0, 0}, {options_.n - 1, options_.n - 1, options_.n - 1}}; }
  Box3 valid_box(int b) const {
    const int x = b % boxes_per_side_, y = (b / boxes_per_side_) % boxes_per_side_;
    const int z = b / (boxes_per_side_ * boxes_per_side_);
    return {{x * bs_, y * bs_, z * bs_},
            {x * bs_ + bs_ - 1, y * bs_ + bs_ - 1, z * bs_ + bs_ - 1}};
  }
  Fab state_fab(hrx_buffer_t buffer, int b) const {
    return {buffer, b * state_box_ * sizeof(double), valid_box(b).grown(2)};
  }

  hrx_buffer_t allocate_bytes(size_t bytes) {
    hrx_buffer_t buffer = nullptr;
    check(hrx_buffer_allocate(streams_[0], bytes, HRX_MEMORY_TYPE_DEVICE_LOCAL,
                              HRX_BUFFER_USAGE_DEFAULT, &buffer),
          "hrx_buffer_allocate");
    buffers_.push_back(buffer);
    return buffer;
  }

  // The_Pinned_Arena: host memory the device can read, mapped at one address.
  Pinned allocate_pinned(size_t bytes) {
    Pinned p;
    check(hrx_buffer_allocate(streams_[0], bytes,
                              HRX_MEMORY_TYPE_HOST_LOCAL | HRX_MEMORY_TYPE_DEVICE_VISIBLE,
                              HRX_BUFFER_USAGE_DEFAULT, &p.buffer),
          "hrx_buffer_allocate (pinned)");
    check(hrx_stream_synchronize(streams_[0]), "hrx_stream_synchronize");
    void* host = nullptr;
    check(hrx_buffer_map(p.buffer, HRX_MAP_READ | HRX_MAP_WRITE | HRX_MAP_MAY_ALIAS,
                         0, bytes, &host),
          "hrx_buffer_map");
    p.host = static_cast<uint8_t*>(host);
    pinned_.push_back(p);
    return p;
  }

  // amrex_loom::stream_synchronize: submit every stream, then wait for one.
  void amrex_sync(hrx_stream_t stream) {
    flush_all();
    check(hrx_stream_synchronize(stream), "hrx_stream_synchronize");
  }
  // MFIter: with several streams, sync the current stream before the loop;
  // Finalize syncs each stream the loop used, one at a time.
  void mfiter_begin() {
    if (streams_.size() > 1 && boxes_ > 1) amrex_sync(streams_[0]);
  }
  void mfiter_end() {
    const int used = std::min<int>(boxes_, static_cast<int>(streams_.size()));
    for (int s = 0; s < used; ++s) amrex_sync(streams_[s]);
  }
  // amrex_loom::memcpy_async from pinned host memory.
  void copy_h2d(hrx_stream_t stream, const void* host, hrx_buffer_t dst,
                size_t offset, size_t bytes) {
    check(hrx_stream_copy_h2d_linear(stream, host, dst, offset, bytes), "hrx_stream_copy_h2d_linear");
  }

  void fp_setup() {
    const int sn = bs_ + 4;
    fabs_ = allocate_bytes(state_box_ * boxes_ * sizeof(double));
    // FabArrayBase::CPC for ParallelCopy(state -> fabs, 0 -> 2 ghosts,
    // periodic): for each destination box, each periodic shift, each source box.
    std::vector<int> shifts[3];
    for (int d = 0; d < 3; ++d) {
      shifts[d] = periodic(d) ? std::vector<int>{-options_.n, 0, options_.n}
                              : std::vector<int>{0};
    }
    std::vector<int> nwarps;
    for (int db = 0; db < boxes_; ++db) {
      const Box3 g = valid_box(db).grown(2);
      for (int sz : shifts[2]) for (int sy : shifts[1]) for (int sx : shifts[0]) {
        const int sh[3] = {sx, sy, sz};
        for (int sb = 0; sb < boxes_; ++sb) {
          Box3 src = valid_box(sb);
          for (int d = 0; d < 3; ++d) src = src.shifted(d, -sh[d]);
          const Box3 region = g & src;
          if (!region.ok()) continue;
          const Box3 dg = g, sg = valid_box(sb).grown(2);
          const int tag[kTagInts] = {
              int(db * state_box_), dg.lo[0], dg.lo[1], dg.lo[2], sn, sn, sn,
              int(sb * state_box_), sg.lo[0], sg.lo[1], sg.lo[2], sn, sn, sn,
              region.lo[0], region.lo[1], region.lo[2],
              region.hi[0], region.hi[1], region.hi[2], sh[0], sh[1], sh[2], 0};
          fp_tags_.insert(fp_tags_.end(), tag, tag + kTagInts);
          nwarps.push_back(fp_ntotwarps_);
          fp_ntotwarps_ += int((region.npts() + kWarp - 1) / kWarp);
          ++fp_ntags_;
        }
      }
    }
    nwarps.push_back(fp_ntotwarps_);
    fp_tag_bytes_ = fp_tags_.size() * sizeof(int);
    fp_nwarps_offset_ = (fp_tag_bytes_ + 15) / 16 * 16;  // Arena::align
    fp_tag_buffer_bytes_ = fp_nwarps_offset_ + nwarps.size() * sizeof(int);
    fp_tag_staging_.assign(fp_tag_buffer_bytes_, 0);
    std::memcpy(fp_tag_staging_.data(), fp_tags_.data(), fp_tag_bytes_);
    std::memcpy(fp_tag_staging_.data() + fp_nwarps_offset_, nwarps.data(),
                nwarps.size() * sizeof(int));
    fp_tags_dev_ = allocate_bytes(fp_tag_buffer_bytes_);
    fp_tags_pin_ = allocate_pinned(fp_tag_buffer_bytes_);
    // BCRec arrays: one slot per StateData::FillBoundary call of a FillPatch,
    // at most one per box plus one per corner fix-up slab.
    fp_bcr_slots_ = 5 * boxes_;
    fp_bcr_dev_ = allocate_bytes(fp_bcr_slots_ * kBcrInts * sizeof(int));
    fp_bcr_pin_ = allocate_pinned(fp_bcr_slots_ * kBcrInts * sizeof(int));
    // Corner fix-up slabs: up to four per box, at most 2 x (bs+4)^2 cells.
    fp_slab_slots_ = 4 * boxes_;
    fp_slab_bytes_ = size_t(2) * sn * sn * kNcons * sizeof(double);
    fp_slab_tmp_ = allocate_bytes(fp_slab_slots_ * fp_slab_bytes_);
    // MultiFab ParallelFor: BoxIndexers of the state's grown boxes, and the
    // state's MultiArray4 entries; AMReX builds both once and caches them.
    std::vector<int> boxes, entries;
    for (int b = 0; b < boxes_; ++b) {
      const Box3 g = valid_box(b).grown(2);
      const int box[kBoxInts] = {g.lo[0], g.lo[1], g.lo[2], sn, sn, sn,
                                 int(g.npts()), 0};
      boxes.insert(boxes.end(), box, box + kBoxInts);
    }
    for (int half = 0; half < 2; ++half) {
      for (int b = 0; b < boxes_; ++b) {
        const Box3 g = valid_box(b).grown(2);
        const int e[kEntInts] = {int(b * state_box_), sn, sn * sn, sn * sn * sn,
                                 g.lo[0], g.lo[1], g.lo[2],
                                 g.hi[0] + 1, g.hi[1] + 1, g.hi[2] + 1, 0, 0, 0, 0};
        entries.insert(entries.end(), e, e + kEntInts);
      }
    }
    fp_entries_ = entries;  // the temporary fabs have the same layout
    fp_boxes_dev_ = allocate_bytes(boxes.size() * sizeof(int));
    fp_dst_ent_dev_ = allocate_bytes(entries.size() * sizeof(int));
    fp_src_ent_dev_ = allocate_bytes(entries.size() * sizeof(int));
    fp_src_a4_dev_ = allocate_bytes(size_t(2) * boxes_ * kArray4Bytes);
    fp_src_a4_pin_ = allocate_pinned(size_t(2) * boxes_ * kArray4Bytes);
    check(hrx_stream_copy_h2d_linear(streams_[0], boxes.data(), fp_boxes_dev_, 0,
                              boxes.size() * sizeof(int)),
          "hrx_stream_copy_h2d_linear");
    check(hrx_stream_copy_h2d_linear(streams_[0], entries.data(), fp_dst_ent_dev_, 0,
                              entries.size() * sizeof(int)),
          "hrx_stream_copy_h2d_linear");
    check(hrx_stream_synchronize(streams_[0]), "hrx_stream_synchronize");
    fp_nblocks_per_box_ = (sn * sn * sn + kBlock - 1) / kBlock;
    std::printf("fillpatch=amrex: %d copy tags, %d warps, %d blocks per ParallelCopy\n",
                fp_ntags_, fp_ntotwarps_, (fp_ntotwarps_ + 3) / 4);
  }

  void fill_patch(hrx_buffer_t state) {
    if (!options_.fillpatch_amrex) {
      fill_ghosts(state);
      return;
    }
    fp_bcr_used_ = 0;
    fp_slab_used_ = 0;
    const int sn = bs_ + 4;
    const double nan = std::numeric_limits<double>::quiet_NaN();

    // FabArray::setDomainBndry(NaN, geom).
    Box3 pdomain = domain();
    for (int d = 0; d < 3; ++d) {
      if (periodic(d)) {
        pdomain.lo[d] -= options_.n;
        pdomain.hi[d] += options_.n;
      }
    }
    mfiter_begin();
    for (int b = 0; b < boxes_; ++b) {
      const Fab fab = state_fab(fabs_, b);
      if (pdomain.contains(fab.box)) continue;
      const uint32_t cells = sn * sn * sn;
      dispatch(stream_for(b), fp_setc_, cells,
               Constants() << cells << bs_ << boxes_per_side_ << b
                           << int(periodic(0)) << int(periodic(1))
                           << int(periodic(2)) << nan,
               {fab.ref()});
    }
    mfiter_end();

    // FillPatchSingleLevel: ParallelCopy from the state, on the current stream.
    std::memcpy(fp_tags_pin_.host, fp_tag_staging_.data(), fp_tag_buffer_bytes_);
    copy_h2d(streams_[0], fp_tags_pin_.host, fp_tags_dev_, 0, fp_tag_buffer_bytes_);
    const uint32_t nblocks = (fp_ntotwarps_ + kBlock / kWarp - 1) / (kBlock / kWarp);
    dispatch(streams_[0], fp_tag_, nblocks * kBlock,
             Constants() << nblocks * kBlock << fp_ntotwarps_ << fp_ntags_ << int(kNcons),
             {hrx_buffer_ref_t{fp_tags_dev_, 0, fp_tag_bytes_},
              hrx_buffer_ref_t{fp_tags_dev_, fp_nwarps_offset_,
                               fp_tag_buffer_bytes_ - fp_nwarps_offset_},
              whole_ref(state, state_box_ * boxes_),
              whole_ref(fabs_, state_box_ * boxes_)});
    amrex_sync(streams_[0]);  // ~TagVector

    // StateDataPhysBCFunct.
    const Box3 dom = domain();
    mfiter_begin();
    for (int b = 0; b < boxes_; ++b) {
      hrx_stream_t stream = stream_for(b);
      const Fab dest = state_fab(fabs_, b);
      const Box3& bx = dest.box;
      bool has_phys_bc = false, is_periodic = false;
      for (int d = 0; d < 3; ++d) {
        const bool touch = bx.lo[d] < dom.lo[d] || bx.hi[d] > dom.hi[d];
        if (periodic(d)) {
          is_periodic = is_periodic || touch;
        } else {
          has_phys_bc = has_phys_bc || touch;
        }
      }
      if (!has_phys_bc) continue;
      state_fill_boundary(stream, bx, dest);
      if (!is_periodic) continue;
      Box3 grown_domain = dom;
      for (int d = 0; d < 3; ++d) {
        if (periodic(d)) continue;
        const int lo = dom.lo[d] - bx.lo[d], hi = bx.hi[d] - dom.hi[d];
        if (lo > 0) grown_domain.lo[d] -= lo;
        if (hi > 0) grown_domain.hi[d] += hi;
      }
      for (int d = 0; d < 3; ++d) {
        if (!periodic(d)) continue;
        for (int side = 0; side < 2; ++side) {
          // lo_slab: bx shifted up a period; hi_slab: down a period.
          const int period = options_.n;
          const Box3 slab = bx.shifted(d, side == 0 ? period : -period) & grown_domain;
          if (!slab.ok()) continue;
          const Fab tmp{fp_slab_tmp_, fp_slab_used_++ * fp_slab_bytes_, slab};
          int shift[3] = {0, 0, 0};
          shift[d] = side == 0 ? -period : period;
          slab_copy(stream, slab, shift, tmp, dest, true);
          state_fill_boundary(stream, slab, tmp);
          slab_copy(stream, slab, shift, tmp, dest, false);
        }
      }
    }
    mfiter_end();

    // MultiFab::Copy(state, fabs, 0, 0, NCONS, 2): fabs.const_arrays() first.
    std::memset(fp_src_a4_pin_.host, 0, size_t(2) * boxes_ * kArray4Bytes);
    copy_h2d(streams_[0], fp_src_a4_pin_.host, fp_src_a4_dev_, 0,
             size_t(2) * boxes_ * kArray4Bytes);
    // Gpu::htod_memcpy of the LOOM entries: pageable, so staged, then a sync.
    std::vector<int> staged = fp_entries_;
    copy_h2d(streams_[0], staged.data(), fp_src_ent_dev_, 0, staged.size() * sizeof(int));
    amrex_sync(streams_[0]);
    const size_t half = size_t(boxes_) * kEntInts * sizeof(int);
    const uint32_t elements = uint32_t(boxes_) * fp_nblocks_per_box_ * kBlock;
    dispatch(streams_[0], fp_mfcopy_, elements,
             Constants() << elements << fp_nblocks_per_box_ << int(kNcons),
             {whole_ref_bytes(fp_boxes_dev_, size_t(boxes_) * kBoxInts * sizeof(int)),
              hrx_buffer_ref_t{fp_src_ent_dev_, half, half},
              hrx_buffer_ref_t{fp_dst_ent_dev_, 0, half},
              whole_ref(fabs_, state_box_ * boxes_),
              whole_ref(state, state_box_ * boxes_)});
    amrex_sync(streams_[0]);
    phase_barrier("fill_ghosts");
  }

  hrx_buffer_ref_t whole_ref_bytes(hrx_buffer_t buffer, size_t bytes) {
    return {buffer, 0, bytes};
  }

  // StateData::FillBoundary(bx, dest, ...) -> cns_bcfill ->
  // GpuBndryFuncFab<CnsFillExtDir>::ccfcdoit with FilccCell.
  void state_fill_boundary(hrx_stream_t stream, const Box3& bx, const Fab& dest) {
    const Box3 dom = domain();
    if (dom.contains(bx)) return;
    Box3 gdomain = dom;
    for (int d = 0; d < 3; ++d) {
      if (periodic(d)) {
        gdomain.lo[d] -= bx.len(d);
        gdomain.hi[d] += bx.len(d);
      }
    }
    if (gdomain.contains(bx)) return;
    // amrex::setBC for each component: outflow -> foextrap in x, interior in
    // the periodic directions; then AsyncArray<BCRec> from pinned memory.
    if (fp_bcr_used_ >= fp_bcr_slots_) {
      std::fprintf(stderr, "cns_loom: out of BCRec slots\n");
      std::exit(1);
    }
    const int slot = fp_bcr_used_++;
    int* bcr = reinterpret_cast<int*>(fp_bcr_pin_.host) + slot * kBcrInts;
    for (int c = 0; c < kNcons; ++c) {
      for (int d = 0; d < 3; ++d) {
        bcr[6 * c + d] = periodic(d) ? 0 : 2;
        bcr[6 * c + 3 + d] = periodic(d) ? 0 : 2;
      }
    }
    const size_t slot_bytes = kBcrInts * sizeof(int);
    copy_h2d(stream, bcr, fp_bcr_dev_, slot * slot_bytes, slot_bytes);
    // Faces first; edges and corners are empty here, since gdomain is grown
    // by the box length in the periodic directions.
    std::vector<Box3> faces;
    for (int d = 0; d < 3; ++d) {
      Box3 lo = gdomain, hi = gdomain;
      lo.hi[d] = gdomain.lo[d] - 1;
      lo.lo[d] = gdomain.lo[d] - bx.len(d);
      hi.lo[d] = gdomain.hi[d] + 1;
      hi.hi[d] = gdomain.hi[d] + bx.len(d);
      for (const Box3& f : {lo, hi}) {
        const Box3 face = f & bx;
        if (face.ok()) faces.push_back(face);
      }
    }
    if (faces.size() != 1) {
      std::fprintf(stderr, "cns_loom: %zu face boxes; only one is ported\n", faces.size());
      std::exit(1);
    }
    const Box3& f = faces[0];
    const uint32_t cells = uint32_t(f.npts());
    dispatch(stream, fp_bc_, cells,
             Constants() << cells << f.lo[0] << f.lo[1] << f.lo[2] << f.len(0)
                         << f.len(1) << f.len(2) << dom.lo[0] << dom.lo[1]
                         << dom.lo[2] << dom.hi[0] << dom.hi[1] << dom.hi[2] << 0
                         << int(kNcons) << dest.box.lo[0] << dest.box.lo[1]
                         << dest.box.lo[2] << dest.box.len(0) << dest.box.len(1)
                         << dest.box.len(2),
             {hrx_buffer_ref_t{fp_bcr_dev_, slot * slot_bytes, slot_bytes},
              dest.ref()});
  }

  void slab_copy(hrx_stream_t stream, const Box3& slab, const int shift[3],
                 const Fab& tmp, const Fab& dest, bool to_tmp) {
    const uint32_t cells = uint32_t(slab.npts());
    dispatch(stream, fp_slab_, cells,
             Constants() << cells << slab.lo[0] << slab.lo[1] << slab.lo[2]
                         << slab.len(0) << slab.len(1) << slab.len(2) << shift[0]
                         << shift[1] << shift[2] << dest.box.lo[0]
                         << dest.box.lo[1] << dest.box.lo[2] << dest.box.len(0)
                         << dest.box.len(1) << dest.box.len(2) << int(kNcons)
                         << int(to_tmp),
             {tmp.ref(), dest.ref()});
  }

  hrx_buffer_t fabs_ = nullptr, fp_tags_dev_ = nullptr, fp_bcr_dev_ = nullptr,
               fp_slab_tmp_ = nullptr, fp_boxes_dev_ = nullptr,
               fp_dst_ent_dev_ = nullptr, fp_src_ent_dev_ = nullptr,
               fp_src_a4_dev_ = nullptr;
  Pinned fp_tags_pin_, fp_bcr_pin_, fp_src_a4_pin_;
  std::vector<Pinned> pinned_;
  std::vector<int> fp_tags_, fp_entries_;
  std::vector<uint8_t> fp_tag_staging_;
  int fp_ntags_ = 0, fp_ntotwarps_ = 0, fp_nblocks_per_box_ = 0;
  size_t fp_tag_bytes_ = 0, fp_nwarps_offset_ = 0, fp_tag_buffer_bytes_ = 0;
  int fp_bcr_slots_ = 0, fp_bcr_used_ = 0, fp_slab_slots_ = 0, fp_slab_used_ = 0;
  size_t fp_slab_bytes_ = 0;

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
    check(hrx_stream_copy_d2h_linear(streams_[0], partial_, 0, partial_host_.data(),
                              partial_host_.size() * sizeof(double)),
          "hrx_stream_copy_d2h_linear");
    // Stream 0 is ordered after every stream's earlier phases.
    if (options_.immediate) flush_all();
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
    check(hrx_stream_copy_d2h_linear(streams_[0], sums_partial_, 0, sums_host_.data(),
                              sums_host_.size() * sizeof(double)),
          "hrx_stream_copy_d2h_linear");
    if (options_.immediate) flush_all();
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
  std::map<hrx_stream_t, bool> unreleased_;
  // --phase-times (with --host-sync): wall time from the end of the previous
  // phase to the end of this phase's host synchronization, per phase name.
  std::map<std::string, std::pair<int, double>> phase_times_;
  std::chrono::steady_clock::time_point phase_start_;
  bool timing_ = false;  // set once the time steps begin
  void phase_done(const char* name) {
    if (!options_.phase_times || !timing_) return;
    const auto now = std::chrono::steady_clock::now();
    auto& entry = phase_times_[name];
    entry.first += 1;
    entry.second += std::chrono::duration<double>(now - phase_start_).count();
    phase_start_ = now;
  }
  hrx_executable_t executable_ = nullptr;
  Kernel init_{"cns_init"}, temp_{"cns_compute_temp"}, fill_{"cns_fill_ghosts"},
      ctoprim_{"cns_ctoprim"}, dudt_{"cns_flux_to_dudt"},
      fp_setc_{"cns_fp_set_complement"}, fp_tag_{"cns_fp_tag_copy"},
      fp_bc_{"cns_fp_bc_face"}, fp_slab_{"cns_fp_slab_copy"},
      fp_mfcopy_{"cns_fp_mf_copy"},
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
    } else if (std::strcmp(arg, "--host-sync=flush") == 0) {
      options.host_sync = true;
      options.flush_first = true;
    } else if (std::strcmp(arg, "--fillpatch=amrex") == 0) {
      options.fillpatch_amrex = true;
    } else if (std::strcmp(arg, "--phase-times") == 0) {
      options.phase_times = true;
    } else if (std::strcmp(arg, "--immediate") == 0) {
      options.immediate = true;
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
                 "[--streams=1] [--host-sync | --host-sync=flush] [--immediate] "
                 "[--graph] [--fillpatch=amrex] [--phase-times]\n");
    return 2;
  }
  Cns cns(options);
  return cns.run();
}
