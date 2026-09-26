# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Analysis tests for exhaustive Loom corpus build qualification."""

load("@rules_testing//lib:analysis_test.bzl", "analysis_test", "test_suite")
load("@rules_testing//lib:truth.bzl", "matching")
load("@rules_testing//lib:util.bzl", "TestingAspectInfo")
load("//loom/build_tools/bazel:defs.bzl", "LoomCorpusInfo")

_FIXTURE = "//loom/build_tools/bazel/test/testdata/corpus"

def _actions_with_mnemonic(actions, mnemonic):
    return [action for action in actions if action.mnemonic == mnemonic]

def _expect_basename(env, files, expected_basename):
    for file in files:
        if file.basename == expected_basename:
            return
    env.fail("expected basename %r in %r" % (expected_basename, files))

def _action_with_argument(env, actions, argument):
    for action in actions:
        if argument in action.argv:
            return action
    env.fail("expected action containing %r in %r" % (argument, actions))
    return None

def _test_program_fans_out_by_profile(name, **kwargs):
    analysis_test(
        name = name,
        impl = _test_program_fans_out_by_profile_impl,
        target = _FIXTURE + ":corpus_fixture",
        **kwargs
    )

def _test_program_fans_out_by_profile_impl(env, target):
    actions = target[TestingAspectInfo].actions
    compile_actions = _actions_with_mnemonic(actions, "LoomCorpusCompile")
    if len(compile_actions) != 2:
        env.fail("expected two profile compile actions, got %r" % compile_actions)
        return
    profile_a = _action_with_argument(env, compile_actions, "--target=fake:a")
    profile_b = _action_with_argument(env, compile_actions, "--target=fake:b")
    for action in [profile_a, profile_b]:
        if "--product=module" not in action.argv:
            env.fail("expected explicit module product in %r" % action.argv)
        if "--compile-report=details" not in action.argv:
            env.fail("expected detailed compile report in %r" % action.argv)
        if "--target=fake:c" in action.argv:
            env.fail("excluded profile C produced an action: %r" % action.argv)
    if "--exclude-root=@unsupported" in profile_a.argv:
        env.fail("profile A inherited profile B's xfail: %r" % profile_a.argv)
    if "--exclude-root=@unsupported" not in profile_b.argv:
        env.fail("profile B did not exclude its xfail: %r" % profile_b.argv)

def _test_program_exposes_outputs_and_xfail_probe(name, **kwargs):
    analysis_test(
        name = name,
        impl = _test_program_exposes_outputs_and_xfail_probe_impl,
        target = _FIXTURE + ":corpus_fixture",
        **kwargs
    )

def _test_program_exposes_outputs_and_xfail_probe_impl(env, target):
    actions = target[TestingAspectInfo].actions
    xfail_actions = _actions_with_mnemonic(actions, "LoomCorpusXfail")
    if len(xfail_actions) != 1:
        env.fail("expected one diagnostic xfail probe, got %r" % xfail_actions)
        return
    xfail_action = xfail_actions[0]
    for expected_arg in [
        "--expected-diagnostic=TARGET/072",
        "--product=module",
        "--root=@unsupported",
        "--target=fake:b",
    ]:
        if expected_arg not in xfail_action.argv:
            env.fail("expected %r in xfail arguments %r" % (expected_arg, xfail_action.argv))

    default_files = target[DefaultInfo].files.to_list()
    if len(default_files) != 5:
        env.fail("expected two artifacts, two reports, and one xfail result: %r" % default_files)
    for basename in [
        "fake-a.artifact",
        "fake-a.compile.json",
        "fake-b.artifact",
        "fake-b.compile.json",
        "fake-b.unsupported.xfail",
    ]:
        _expect_basename(env, default_files, basename)
    if len(target[OutputGroupInfo].artifacts.to_list()) != 2:
        env.fail("expected two artifact output-group files")
    if len(target[OutputGroupInfo].compile_reports.to_list()) != 2:
        env.fail("expected two compile-report output-group files")
    if len(target[OutputGroupInfo].xfail_results.to_list()) != 1:
        env.fail("expected one xfail-result output-group file")

def _test_aggregate_collects_program_outputs(name, **kwargs):
    analysis_test(
        name = name,
        impl = _test_aggregate_collects_program_outputs_impl,
        target = _FIXTURE + ":corpus",
        **kwargs
    )

def _test_aggregate_collects_program_outputs_impl(env, target):
    corpus = target[LoomCorpusInfo]
    if len(corpus.sources.to_list()) != 2:
        env.fail("expected two corpus sources, got %r" % corpus.sources.to_list())
    if len(corpus.artifacts.to_list()) != 4:
        env.fail("expected four corpus artifacts, got %r" % corpus.artifacts.to_list())
    if len(corpus.compile_reports.to_list()) != 4:
        env.fail("expected four corpus reports, got %r" % corpus.compile_reports.to_list())
    if len(corpus.qualification_results.to_list()) != 1:
        env.fail(
            "expected one corpus qualification result, got %r" %
            corpus.qualification_results.to_list(),
        )
    if len(target[DefaultInfo].files.to_list()) != 9:
        env.fail("aggregate default outputs must request every qualification action")

def _test_unsupported_product_fails(name, **kwargs):
    analysis_test(
        name = name,
        expect_failure = True,
        impl = _test_unsupported_product_fails_impl,
        target = _FIXTURE + ":unsupported_fixture",
        **kwargs
    )

def _test_unsupported_product_fails_impl(env, target):
    env.expect.that_target(target).failures().contains_predicate(
        matching.contains(
            "unsupported corpus product \"kernel\"; this rule currently supports only 'module'",
        ),
    )

def loom_corpus_rules_test_suite(name):
    test_suite(
        name = name,
        tests = [
            _test_aggregate_collects_program_outputs,
            _test_program_exposes_outputs_and_xfail_probe,
            _test_program_fans_out_by_profile,
            _test_unsupported_product_fails,
        ],
    )
