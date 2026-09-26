# AMDGPU: `cfg-converge` leaves a shared failure branch when the success arm uses a locally defined view

## Bug

A valid kernel with two conditional branches to the same failure block is rejected by the default AMDGPU pipeline with `TARGET/034 masked_region_single_entry`. The pipeline includes `cfg-converge`, but that pass does not factor the branches when a `buffer.view` created between them is used after the second branch. The same kernel compiles if the view is created before the first branch.

This blocks lowering of the chemistry `prepare_grid_timestep_kernel`: its success path stores through a view created while checking the second condition. The failure is a control-flow lowering limitation, independent of the kernel's f64 arithmetic.

## Reproducer

Save this as `shared_failure_live_view.loom`:

```loom
amdgpu.target<gfx942> @target

kernel.def target(@target) @shared_failure_live_view() {
  %one = index.constant 1 : index
  %size = index.constant 64 : index
  kernel.launch.config workgroups(%one, %one, %one) workgroup_size(%size, %one, %one) : index
} launch(%input: buffer, %step: i32, %output: buffer) {
  %lane = kernel.workitem.id<x> : index
  %zero_offset = index.constant 0 : offset
  %input_view = buffer.view %input[%zero_offset] : buffer -> view<128xi32>
  %first_value = view.load %input_view[%lane] : view<128xi32> -> i32
  %valid = scalar.cmpi eq, %first_value, %step : i32
  %true = scalar.constant true : i1
  %bad = scalar.xori %valid, %true : i1
  cfg.cond_br %bad, ^failure, ^check_second
^check_second:
  %stride = index.constant 64 : index
  %second_index = index.add %lane, %stride : index
  %second_value = view.load %input_view[%second_index] : view<128xi32> -> i32
  %zero = scalar.constant 0 : i32
  %second_bad = scalar.cmpi sgt, %second_value, %zero : i32
  %success_view = buffer.view %output[%zero_offset] : buffer -> view<1xi32>
  cfg.cond_br %second_bad, ^failure, ^success
^failure:
  %output_view = buffer.view %output[%zero_offset] : buffer -> view<1xi32>
  %bad_code = scalar.constant -1 : i32
  view.store %bad_code, %output_view[0] : i32, view<1xi32>
  cfg.br ^done
^success:
  view.store %second_value, %success_view[0] : i32, view<1xi32>
  cfg.br ^done
^done:
  kernel.return
}
```

Run the default pipeline:

```sh
loom-compile shared_failure_live_view.loom --product=kernel --format=amdgpu-hsaco --target=amdgpu:gfx942 --root=shared_failure_live_view --output=/tmp/shared_failure_live_view.hsaco
```

**Actual:** compilation fails at both `cfg.cond_br` operations:

```text
error [TARGET/034]: ... branch lowering constraint 'masked_region_single_entry' is not satisfied
```

**Expected:** the valid kernel compiles to an AMDGPU code object, preserving the failure store and success store.

## Reduction and likely cause

- Without the success-path `view.store`, the `cfg-converge,source-to-low` sequence gets past `TARGET/034` in the full chemistry kernel. Removing its other success-path store does not.
- In this standalone reproducer, moving only `%success_view = buffer.view ...` to the entry block, before the first `cfg.cond_br`, makes the **default pipeline** compile. Leaving the view in `^check_second` reproduces `TARGET/034`.
- `loom/src/loom/transforms/cfg/cfg_converge.c` limits values carried across its predicate join to scalars (`loom_cfg_converge_capture`, `!loom_type_is_scalar(value->type)`). Its preflight then declines the transformation when the success arm needs this view. `source-to-low` receives the original shared-entry CFG and rejects it.

The compiler needs an ownership-preserving way to factor this control flow when the arm has a live view, or an equivalent canonicalization that moves safe view construction before the branch. A regression should cover the default pipeline and verify both paths still perform their intended stores.
