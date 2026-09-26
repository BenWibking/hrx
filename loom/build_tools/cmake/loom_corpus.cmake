# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/licenses/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Build-only qualification for tested Loom corpus programs.
function(loom_corpus)
  cmake_parse_arguments(
    _RULE
    ""
    "NAME"
    "SRCS;TARGETS;PRODUCTS;XFAILS;EXCLUDES"
    ${ARGN}
  )
  if(NOT _RULE_NAME)
    message(FATAL_ERROR "loom_corpus requires NAME")
  endif()
  if(NOT _RULE_SRCS)
    message(FATAL_ERROR "loom_corpus(${_RULE_NAME}) requires SRCS")
  endif()
  if(NOT _RULE_TARGETS)
    message(FATAL_ERROR "loom_corpus(${_RULE_NAME}) requires TARGETS")
  endif()

  file(GLOB _INVENTORY
    CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.loom"
  )
  set(_DECLARED_SRCS ${_RULE_SRCS})
  list(SORT _INVENTORY)
  list(SORT _DECLARED_SRCS)
  if(NOT "${_INVENTORY}" STREQUAL "${_DECLARED_SRCS}")
    message(FATAL_ERROR
      "loom_corpus(${_RULE_NAME}) sources do not match the package inventory; "
      "declared '${_DECLARED_SRCS}', found '${_INVENTORY}'")
  endif()

  list(LENGTH _RULE_PRODUCTS _PRODUCT_VALUE_COUNT)
  list(LENGTH _RULE_SRCS _SOURCE_COUNT)
  math(EXPR _EXPECTED_PRODUCT_VALUE_COUNT "${_SOURCE_COUNT} * 2")
  if(NOT _PRODUCT_VALUE_COUNT EQUAL _EXPECTED_PRODUCT_VALUE_COUNT)
    message(FATAL_ERROR
      "loom_corpus(${_RULE_NAME}) PRODUCTS must pair every source with one product")
  endif()
  set(_PRODUCT_SOURCES)
  set(_PRODUCT_KINDS)
  set(_INDEX 0)
  while(_INDEX LESS _PRODUCT_VALUE_COUNT)
    list(GET _RULE_PRODUCTS ${_INDEX} _SOURCE)
    math(EXPR _INDEX "${_INDEX} + 1")
    list(GET _RULE_PRODUCTS ${_INDEX} _PRODUCT)
    math(EXPR _INDEX "${_INDEX} + 1")
    if(NOT _SOURCE IN_LIST _RULE_SRCS)
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) product names unknown source ${_SOURCE}")
    endif()
    if(_SOURCE IN_LIST _PRODUCT_SOURCES)
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) repeats product source ${_SOURCE}")
    endif()
    if(NOT _PRODUCT STREQUAL "module")
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) has unsupported product '${_PRODUCT}'; "
        "only 'module' is supported")
    endif()
    list(APPEND _PRODUCT_SOURCES "${_SOURCE}")
    list(APPEND _PRODUCT_KINDS "${_PRODUCT}")
  endwhile()

  set(_PROFILES)
  foreach(_TARGET_REF IN LISTS _RULE_TARGETS)
    iree_package_target_name(_TARGET "${_TARGET_REF}")
    if(NOT TARGET "${_TARGET}")
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) names unknown target ${_TARGET_REF}")
    endif()
    get_target_property(_TARGET_PROFILES "${_TARGET}" LOOM_TARGET_PROFILES)
    if(NOT _TARGET_PROFILES)
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) target ${_TARGET_REF} is not a Loom profile or set")
    endif()
    list(APPEND _PROFILES ${_TARGET_PROFILES})
  endforeach()
  list(REMOVE_DUPLICATES _PROFILES)
  set(_PROFILE_STEMS)
  foreach(_PROFILE IN LISTS _PROFILES)
    get_target_property(_COMPILER_TARGET "${_PROFILE}" LOOM_COMPILER_TARGET)
    string(REGEX REPLACE "[:/+.]+" "-" _PROFILE_STEM "${_COMPILER_TARGET}")
    if(_PROFILE_STEM IN_LIST _PROFILE_STEMS)
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) target profiles collide at output identity "
        "${_PROFILE_STEM}")
    endif()
    list(APPEND _PROFILE_STEMS "${_PROFILE_STEM}")
  endforeach()

  list(LENGTH _RULE_XFAILS _XFAIL_VALUE_COUNT)
  math(EXPR _XFAIL_REMAINDER "${_XFAIL_VALUE_COUNT} % 4")
  if(NOT _XFAIL_REMAINDER EQUAL 0)
    message(FATAL_ERROR
      "loom_corpus(${_RULE_NAME}) XFAILS must contain profile/source/root/diagnostic groups")
  endif()
  set(_INDEX 0)
  while(_INDEX LESS _XFAIL_VALUE_COUNT)
    list(GET _RULE_XFAILS ${_INDEX} _PROFILE_REF)
    math(EXPR _INDEX "${_INDEX} + 1")
    list(GET _RULE_XFAILS ${_INDEX} _SOURCE)
    math(EXPR _INDEX "${_INDEX} + 1")
    list(GET _RULE_XFAILS ${_INDEX} _ROOT)
    math(EXPR _INDEX "${_INDEX} + 1")
    list(GET _RULE_XFAILS ${_INDEX} _DIAGNOSTIC)
    math(EXPR _INDEX "${_INDEX} + 1")
    iree_package_target_name(_PROFILE "${_PROFILE_REF}")
    if(NOT _PROFILE IN_LIST _PROFILES)
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) has an xfail for unselected profile ${_PROFILE_REF}")
    endif()
    get_property(_IS_PROFILE TARGET "${_PROFILE}" PROPERTY LOOM_COMPILER_TARGET SET)
    if(NOT _IS_PROFILE)
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) xfail key ${_PROFILE_REF} is not a concrete profile")
    endif()
    if(NOT _SOURCE IN_LIST _RULE_SRCS)
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) xfail names unknown source ${_SOURCE}")
    endif()
    if(NOT _ROOT MATCHES "^@")
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) xfail root ${_ROOT} must begin with '@'")
    endif()
    if(NOT _DIAGNOSTIC)
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) xfail for ${_ROOT} requires a diagnostic")
    endif()
  endwhile()

  list(LENGTH _RULE_EXCLUDES _EXCLUDE_VALUE_COUNT)
  math(EXPR _EXCLUDE_REMAINDER "${_EXCLUDE_VALUE_COUNT} % 3")
  if(NOT _EXCLUDE_REMAINDER EQUAL 0)
    message(FATAL_ERROR
      "loom_corpus(${_RULE_NAME}) EXCLUDES must contain profile/source/reason groups")
  endif()
  set(_INDEX 0)
  while(_INDEX LESS _EXCLUDE_VALUE_COUNT)
    list(GET _RULE_EXCLUDES ${_INDEX} _PROFILE_REF)
    math(EXPR _INDEX "${_INDEX} + 1")
    list(GET _RULE_EXCLUDES ${_INDEX} _SOURCE)
    math(EXPR _INDEX "${_INDEX} + 1")
    list(GET _RULE_EXCLUDES ${_INDEX} _REASON)
    math(EXPR _INDEX "${_INDEX} + 1")
    iree_package_target_name(_PROFILE "${_PROFILE_REF}")
    if(NOT _PROFILE IN_LIST _PROFILES)
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) excludes unselected profile ${_PROFILE_REF}")
    endif()
    get_property(_IS_PROFILE TARGET "${_PROFILE}" PROPERTY LOOM_COMPILER_TARGET SET)
    if(NOT _IS_PROFILE)
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) exclude key ${_PROFILE_REF} is not a concrete profile")
    endif()
    if(NOT _SOURCE IN_LIST _RULE_SRCS)
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) exclude names unknown source ${_SOURCE}")
    endif()
    if(NOT _REASON)
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) exclude for ${_SOURCE} requires a reason")
    endif()
  endwhile()

  set(_INDEX 0)
  while(_INDEX LESS _XFAIL_VALUE_COUNT)
    list(GET _RULE_XFAILS ${_INDEX} _XFAIL_PROFILE_REF)
    math(EXPR _INDEX "${_INDEX} + 1")
    list(GET _RULE_XFAILS ${_INDEX} _XFAIL_SOURCE)
    math(EXPR _INDEX "${_INDEX} + 3")
    iree_package_target_name(_XFAIL_PROFILE "${_XFAIL_PROFILE_REF}")
    set(_EXCLUDE_INDEX 0)
    while(_EXCLUDE_INDEX LESS _EXCLUDE_VALUE_COUNT)
      list(GET _RULE_EXCLUDES ${_EXCLUDE_INDEX} _EXCLUDE_PROFILE_REF)
      math(EXPR _EXCLUDE_INDEX "${_EXCLUDE_INDEX} + 1")
      list(GET _RULE_EXCLUDES ${_EXCLUDE_INDEX} _EXCLUDE_SOURCE)
      math(EXPR _EXCLUDE_INDEX "${_EXCLUDE_INDEX} + 2")
      iree_package_target_name(_EXCLUDE_PROFILE "${_EXCLUDE_PROFILE_REF}")
      if(_XFAIL_PROFILE STREQUAL _EXCLUDE_PROFILE AND
         _XFAIL_SOURCE STREQUAL _EXCLUDE_SOURCE)
        message(FATAL_ERROR
          "loom_corpus(${_RULE_NAME}) cannot both exclude and xfail "
          "${_XFAIL_SOURCE} for ${_XFAIL_PROFILE_REF}")
      endif()
    endwhile()
  endwhile()

  iree_package_name(_PACKAGE_NAME)
  set(_CORPUS_TARGET "${_PACKAGE_NAME}_${_RULE_NAME}")
  set(_PROGRAM_TARGETS)
  set(_ALL_ARTIFACTS)
  set(_ALL_REPORTS)
  set(_ALL_XFAIL_RESULTS)
  foreach(_SOURCE IN LISTS _RULE_SRCS)
    list(FIND _PRODUCT_SOURCES "${_SOURCE}" _PRODUCT_INDEX)
    list(GET _PRODUCT_KINDS ${_PRODUCT_INDEX} _PRODUCT)
    string(REGEX REPLACE "\\.loom$" "" _SOURCE_STEM "${_SOURCE}")
    string(REGEX REPLACE "[/\\.]" "_" _SOURCE_STEM "${_SOURCE_STEM}")
    set(_PROGRAM_TARGET "${_CORPUS_TARGET}_${_SOURCE_STEM}")
    set(_PROGRAM_OUTPUTS)
    set(_PROGRAM_ARTIFACTS)
    set(_PROGRAM_REPORTS)
    set(_PROGRAM_XFAIL_RESULTS)
    set(_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/${_RULE_NAME}_${_SOURCE_STEM}")
    set(_SUBJECT_MODULE_NAME "${_RULE_NAME}_${_SOURCE_STEM}_subjects")
    loom_module(
      NAME "${_SUBJECT_MODULE_NAME}"
      SRCS "${_SOURCE}"
      MODE link
      OUTPUT_FORMAT bc
      INCLUDE_INPUT_TESTS
      STRIP_CHECK
    )
    iree_package_target_name(_SUBJECT_MODULE_TARGET "::${_SUBJECT_MODULE_NAME}")
    get_target_property(_SUBJECT_MODULE "${_SUBJECT_MODULE_TARGET}" LOOM_MODULE_FILE)
    set(_AVAILABLE_PROFILE_COUNT 0)
    foreach(_PROFILE IN LISTS _PROFILES)
      get_target_property(_AVAILABLE "${_PROFILE}" LOOM_PROFILE_AVAILABLE)
      if(NOT _AVAILABLE)
        continue()
      endif()
      math(EXPR _AVAILABLE_PROFILE_COUNT "${_AVAILABLE_PROFILE_COUNT} + 1")
      get_target_property(_COMPILER_TARGET "${_PROFILE}" LOOM_COMPILER_TARGET)
      string(REGEX REPLACE "[:/+.]+" "-" _PROFILE_STEM "${_COMPILER_TARGET}")

      set(_IS_EXCLUDED FALSE)
      set(_EXCLUDE_INDEX 0)
      while(_EXCLUDE_INDEX LESS _EXCLUDE_VALUE_COUNT)
        list(GET _RULE_EXCLUDES ${_EXCLUDE_INDEX} _PROFILE_REF)
        math(EXPR _EXCLUDE_INDEX "${_EXCLUDE_INDEX} + 1")
        list(GET _RULE_EXCLUDES ${_EXCLUDE_INDEX} _EXCLUDE_SOURCE)
        math(EXPR _EXCLUDE_INDEX "${_EXCLUDE_INDEX} + 2")
        iree_package_target_name(_EXCLUDE_PROFILE "${_PROFILE_REF}")
        if(_PROFILE STREQUAL _EXCLUDE_PROFILE AND
           _SOURCE STREQUAL _EXCLUDE_SOURCE)
          set(_IS_EXCLUDED TRUE)
        endif()
      endwhile()
      if(_IS_EXCLUDED)
        continue()
      endif()

      set(_XFAIL_ROOTS)
      set(_XFAIL_DIAGNOSTICS)
      set(_XFAIL_INDEX 0)
      while(_XFAIL_INDEX LESS _XFAIL_VALUE_COUNT)
        list(GET _RULE_XFAILS ${_XFAIL_INDEX} _PROFILE_REF)
        math(EXPR _XFAIL_INDEX "${_XFAIL_INDEX} + 1")
        list(GET _RULE_XFAILS ${_XFAIL_INDEX} _XFAIL_SOURCE)
        math(EXPR _XFAIL_INDEX "${_XFAIL_INDEX} + 1")
        list(GET _RULE_XFAILS ${_XFAIL_INDEX} _ROOT)
        math(EXPR _XFAIL_INDEX "${_XFAIL_INDEX} + 1")
        list(GET _RULE_XFAILS ${_XFAIL_INDEX} _DIAGNOSTIC)
        math(EXPR _XFAIL_INDEX "${_XFAIL_INDEX} + 1")
        iree_package_target_name(_XFAIL_PROFILE "${_PROFILE_REF}")
        if(_PROFILE STREQUAL _XFAIL_PROFILE AND
           _SOURCE STREQUAL _XFAIL_SOURCE)
          list(APPEND _XFAIL_ROOTS "${_ROOT}")
          list(APPEND _XFAIL_DIAGNOSTICS "${_DIAGNOSTIC}")
        endif()
      endwhile()

      set(_ARTIFACT "${_OUTPUT_DIR}/${_PROFILE_STEM}.artifact")
      set(_REPORT "${_OUTPUT_DIR}/${_PROFILE_STEM}.compile.json")
      set(_EXCLUDE_ARGS)
      foreach(_ROOT IN LISTS _XFAIL_ROOTS)
        list(APPEND _EXCLUDE_ARGS "--exclude-root=${_ROOT}")
      endforeach()
      add_custom_command(
        OUTPUT "${_ARTIFACT}" "${_REPORT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_OUTPUT_DIR}"
        COMMAND "$<TARGET_FILE:loom::tools::loom-compile>"
          "${_SUBJECT_MODULE}"
          "--product=${_PRODUCT}"
          "--target=${_COMPILER_TARGET}"
          ${_EXCLUDE_ARGS}
          "--output=${_ARTIFACT}"
          "--compile-report=details"
          "--compile-report-output=${_REPORT}"
        DEPENDS loom::tools::loom-compile "${_SUBJECT_MODULE}"
        COMMENT "Compiling corpus program ${_SOURCE} for ${_COMPILER_TARGET}"
        VERBATIM
      )
      list(APPEND _PROGRAM_OUTPUTS "${_ARTIFACT}" "${_REPORT}")
      list(APPEND _PROGRAM_ARTIFACTS "${_ARTIFACT}")
      list(APPEND _PROGRAM_REPORTS "${_REPORT}")

      list(LENGTH _XFAIL_ROOTS _PROFILE_XFAIL_COUNT)
      if(_PROFILE_XFAIL_COUNT GREATER 0)
        set(_XFAIL_ARGS)
        set(_PROFILE_XFAIL_INDEX 0)
        while(_PROFILE_XFAIL_INDEX LESS _PROFILE_XFAIL_COUNT)
          list(GET _XFAIL_ROOTS ${_PROFILE_XFAIL_INDEX} _ROOT)
          list(GET _XFAIL_DIAGNOSTICS ${_PROFILE_XFAIL_INDEX} _DIAGNOSTIC)
          math(EXPR _PROFILE_XFAIL_INDEX "${_PROFILE_XFAIL_INDEX} + 1")
          list(APPEND _XFAIL_ARGS
            "--expected-root=${_ROOT}"
            "--expected-diagnostic=${_DIAGNOSTIC}"
          )
        endwhile()
        set(_XFAIL_RESULT "${_OUTPUT_DIR}/${_PROFILE_STEM}.xfails")
        add_custom_command(
          OUTPUT "${_XFAIL_RESULT}"
          COMMAND "${CMAKE_COMMAND}" -E make_directory "${_OUTPUT_DIR}"
          COMMAND "$<TARGET_FILE:loom::build_tools::corpus::loom-corpus-compile-xfails>"
            "--compiler=$<TARGET_FILE:loom::tools::loom-compile>"
            "--stamp-output=${_XFAIL_RESULT}"
            ${_XFAIL_ARGS}
            "${_SUBJECT_MODULE}"
            "--product=${_PRODUCT}"
            "--target=${_COMPILER_TARGET}"
          DEPENDS
            loom::build_tools::corpus::loom-corpus-compile-xfails
            loom::tools::loom-compile
            "${_SUBJECT_MODULE}"
          COMMENT "Probing corpus diagnostic xfails in ${_SOURCE} for ${_COMPILER_TARGET}"
          VERBATIM
        )
        list(APPEND _PROGRAM_OUTPUTS "${_XFAIL_RESULT}")
        list(APPEND _PROGRAM_XFAIL_RESULTS "${_XFAIL_RESULT}")
      endif()
    endforeach()

    if(_AVAILABLE_PROFILE_COUNT GREATER 0 AND NOT _PROGRAM_OUTPUTS)
      message(FATAL_ERROR
        "loom_corpus(${_RULE_NAME}) excludes every available profile for ${_SOURCE}")
    endif()
    add_custom_target("${_PROGRAM_TARGET}" DEPENDS ${_PROGRAM_OUTPUTS})
    set_target_properties("${_PROGRAM_TARGET}" PROPERTIES
      LOOM_CORPUS_ARTIFACTS "${_PROGRAM_ARTIFACTS}"
      LOOM_CORPUS_COMPILE_REPORTS "${_PROGRAM_REPORTS}"
      LOOM_CORPUS_XFAIL_RESULTS "${_PROGRAM_XFAIL_RESULTS}"
    )
    list(APPEND _PROGRAM_TARGETS "${_PROGRAM_TARGET}")
    list(APPEND _ALL_ARTIFACTS ${_PROGRAM_ARTIFACTS})
    list(APPEND _ALL_REPORTS ${_PROGRAM_REPORTS})
    list(APPEND _ALL_XFAIL_RESULTS ${_PROGRAM_XFAIL_RESULTS})
  endforeach()

  add_custom_target("${_CORPUS_TARGET}" DEPENDS ${_PROGRAM_TARGETS})
  set_target_properties("${_CORPUS_TARGET}" PROPERTIES
    LOOM_CORPUS_ARTIFACTS "${_ALL_ARTIFACTS}"
    LOOM_CORPUS_COMPILE_REPORTS "${_ALL_REPORTS}"
    LOOM_CORPUS_XFAIL_RESULTS "${_ALL_XFAIL_RESULTS}"
  )
endfunction()
