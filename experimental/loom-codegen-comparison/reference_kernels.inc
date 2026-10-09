// SPDX-License-Identifier: BSD-3-Clause
// Generated original kernel bodies for serial-lane differential testing.
void reference_prepare_grid_timestep_kernel(CollapseState* cells, int num_cells,
                                             int completed_global_steps,
                                             integrators::Real grid_time, int step,
                                             bool perturb,
                                             integrators::Real* dt_candidates,
                                             int* failure_code, int cell_index){
    const int cell = cell_index;
    if (cell >= num_cells) {
        return;
    }
    dt_candidates[cell] = std::numeric_limits<integrators::Real>::max();
    if (*failure_code != static_cast<int>(integrators::IntegratorResult::SUCCESS)) {
        return;
    }

    CollapseState& state = cells[cell];
    if (state.completed_steps < completed_global_steps) {
        return;
    }
    if (state.completed_steps != completed_global_steps || state.time != grid_time) {
        chemistry::atomic_cas(failure_code, static_cast<int>(integrators::IntegratorResult::SUCCESS),
                  static_cast<int>(integrators::IntegratorResult::BAD_INPUTS));
        return;
    }

    apply_perturbation(state, cell, step, perturb);
    const integrators::Real dt = collapse_timestep(state);
    if (!valid_positive(dt) || !valid_positive(state.density_driver)) {
        chemistry::atomic_cas(failure_code, static_cast<int>(integrators::IntegratorResult::SUCCESS),
                  static_cast<int>(integrators::IntegratorResult::BAD_INPUTS));
        return;
    }
    if (dt < 10.0) {
        const integrators::Real tff = dt / tff_reduc;
        state.density_driver += dt * (state.density_driver / tff);
        return;
    }
    dt_candidates[cell] = dt;
}
void reference_advance_collapse_gridwide_kernel(
    CollapseState* cells, int num_cells, int completed_global_steps,
    integrators::Real next_grid_time, integrators::Real dt_grid,
    int* integrated_count, int* failure_code, int cell_index){
    const int cell = cell_index;
    if (cell >= num_cells ||
        *failure_code != static_cast<int>(integrators::IntegratorResult::SUCCESS)) {
        return;
    }

    CollapseState& state = cells[cell];
    if (state.completed_steps != completed_global_steps) {
        return;
    }

    const integrators::Real local_dt = collapse_timestep(state);
    if (local_dt < 10.0) {
        return;
    }
    const integrators::Real old_density = state.density_driver;
    const integrators::Real tff = local_dt / tff_reduc;
    state.density_driver += dt_grid * (state.density_driver / tff);
    if (!valid_positive(state.density_driver) || state.density_driver > 2.0e-6) {
        return;
    }

    const integrators::Real density_ratio = state.density_driver / old_density;
    for (auto& xn : state.current.xn) {
        xn *= density_ratio;
    }
    state.current.rho *= density_ratio;

    auto failure = integrators::IntegratorResult::SUCCESS;
    const auto result = burn_ros2s(state.current, dt_grid, state.stats);
    if (result != integrators::IntegratorResult::SUCCESS) {
        failure = result;
    } else {
        pc::floor_and_normalize_number_densities(state.current);
        pc::balance_charge(state.current);
        pc::floor_and_normalize_number_densities(state.current);
        pc::eos_re(state.current);
        state.time = next_grid_time;
        state.completed_steps = completed_global_steps + 1;
        chemistry::atomic_add(integrated_count, 1);
    }
    if (failure != integrators::IntegratorResult::SUCCESS) {
        chemistry::atomic_cas(failure_code, static_cast<int>(integrators::IntegratorResult::SUCCESS),
                  static_cast<int>(failure));
    }
}
