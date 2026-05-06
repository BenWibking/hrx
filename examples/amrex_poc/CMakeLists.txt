# Copyright 2026 The HRX Authors
# SPDX-License-Identifier: Apache-2.0
#
# Optional AMReX-on-HRX proof-of-concept executable.

find_package(AMReX CONFIG REQUIRED)

add_executable(amrex_hrx_poc main.cpp)
target_link_libraries(amrex_hrx_poc PRIVATE AMReX::amrex)
target_compile_features(amrex_hrx_poc PRIVATE cxx_std_17)

if(TARGET amdhip64)
  add_dependencies(amrex_hrx_poc amdhip64)
endif()
