# Advance-kernel allocation reproducer

This compile-only reproducer isolates the scratch-store/read pattern observed
while reducing the chemistry advance kernel's gfx90a allocation failure. It
needs only `loom-compile` with AMDGPU support; neither HIP nor a GPU is required.
It contains no chemistry, math recipes, loops, branches, atomics, or imports.

Run directly:

```sh
loom-compile repro.loom --pipeline=none --format=amdgpu-hsaco \
  --target=amdgpu:gfx90a --output=/tmp/repro.hsaco
```

At source revision `b0267f903b08081db399892bf24a79fc351f9a1c`, this exits 1 with
`BACKEND/005`, `amdgpu.vgpr`, budget 256, peak 262, and
`spill-traffic-register-exhausted`. The original chemistry kernel reported
peak 259; this reproducer preserves the target, native register budget,
failure category, and scratch-traffic pattern, rather than the entire original
allocation history. It is a structural reproducer, not a mechanically
deletion-minimized copy of the original kernel or a proof of a shared root cause.

The kernel initializes 249 distinct eight-byte scratch slots, then reads each
slot for a global store. Each initialized value consists of two copies of a
32-bit constant. Keeping the reads makes the scratch stores observable to the
compiler. Compilation enters spill repair and eventually fails to allocate a
two-register concatenation. The reported peak describes the failed allocation
attempt, including repair traffic; it is not an intrinsic register lower bound
for this algorithm.

Check the failure and its controls together:

```sh
python3 reproduce.py /path/to/loom-compile --output-dir=/tmp/advance-repro-check
```

The script succeeds only when the 249-slot case reports the specific allocation
error and both controls emit nonempty HSACOs:

* 248 slots, with writes followed by reads.
* 249 slots, with each write immediately followed by its read and global store.

`generate.py` generates the checked-in input and the controls:

```sh
python3 generate.py /tmp/repro.loom
python3 generate.py /tmp/control.loom --slots 248
python3 generate.py /tmp/interleaved.loom --interleave
```

The slot count is the reduction parameter. All counts from 1 through 248 were
checked and compiled successfully; 249 is the first failing count in this
generated family at the stated revision. No claim of global IR minimality is
made. The input deliberately starts at target-low IR and uses `--pipeline=none`
to exercise native scheduling, allocation, and spill repair directly.

See `VALIDATION.txt` for the compiler identity and measured check results.
