cmake_minimum_required(VERSION 3.20)
project(loom_chemistry_reproducer LANGUAGES CXX)
enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CHEM_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(CHEM_GENERATED_SOURCES
  ${CHEM_GENERATED_DIR}/reproducer.cpp
  ${CHEM_GENERATED_DIR}/integrate.inc
  ${CHEM_GENERATED_DIR}/reference_kernels.inc)
add_custom_command(
  OUTPUT ${CHEM_GENERATED_SOURCES}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate.py
    --output-dir=${CHEM_GENERATED_DIR}
  DEPENDS generate.py reference.cpp
  COMMENT "Generating Loom chemistry sources"
  VERBATIM)
set_source_files_properties(${CHEM_GENERATED_SOURCES} PROPERTIES HEADER_FILE_ONLY ON)
add_executable(chemistry-validate validate.cpp ${CHEM_GENERATED_SOURCES})
target_include_directories(chemistry-validate PRIVATE
  ${CHEM_GENERATED_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(chemistry-validate PRIVATE cxx_std_20)
if(MSVC)
  message(FATAL_ERROR "Validation currently requires Clang or GCC (native atomic builtins and LP64)")
endif()
target_compile_options(chemistry-validate PRIVATE -ffp-contract=off)
option(CHEM_SANITIZE "Enable address and undefined-behavior sanitizers" OFF)
if(CHEM_SANITIZE)
  target_compile_options(chemistry-validate PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(chemistry-validate PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME numerical-equivalence COMMAND chemistry-validate)
if(COMMAND iree_register_test_build_targets)
  iree_register_test_build_targets(numerical-equivalence TARGETS chemistry-validate)
endif()
set(LOOM_IMPORT_CXX_TOOL "" CACHE FILEPATH "Optional path to the native loom-import-cxx executable")
if(LOOM_IMPORT_CXX_TOOL)
  add_test(NAME importer-compatibility COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/check_import.py ${LOOM_IMPORT_CXX_TOOL})
  if(COMMAND iree_register_test_build_targets)
    iree_register_test_build_targets(importer-compatibility)
  endif()
endif()
set(LOOM_COMPILE_TOOL "" CACHE FILEPATH "Optional path to the native loom-compile executable")
if(LOOM_IMPORT_CXX_TOOL AND LOOM_COMPILE_TOOL)
  # XFAIL: passes while the production-sized advance kernel fails allocation.
  add_test(NAME production-advance-compile COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/check_compile.py ${LOOM_IMPORT_CXX_TOOL} ${LOOM_COMPILE_TOOL})
  set_tests_properties(production-advance-compile PROPERTIES TIMEOUT 1800)
  if(COMMAND iree_register_test_build_targets)
    iree_register_test_build_targets(production-advance-compile)
  endif()
endif()
