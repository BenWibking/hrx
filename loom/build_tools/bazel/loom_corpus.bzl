# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Build-only qualification rules for tested Loom corpus programs."""

load("//build_tools/bazel:glob.bzl", "iree_checked_glob")
load(":loom_linking.bzl", "loom_linking")
load(
    ":loom_target_profile.bzl",
    "LoomTargetProfileInfo",
    "LoomTargetSetInfo",
)

_LOOM_COMPILE_TOOLCHAIN_TYPE = Label("//loom/build_tools/bazel:compile_toolchain_type")
_LOOM_LINK_TOOLCHAIN_TYPE = Label("//loom/build_tools/bazel:link_toolchain_type")

_LoomCorpusProgramInfo = provider(
    doc = "Outputs and source identity for one corpus program.",
    fields = {
        "artifacts": "Depset of deployable artifacts across target profiles.",
        "compile_reports": "Depset of detailed compile reports.",
        "qualification_results": "Depset of batched diagnostic-xfail results.",
        "sources": "Depset containing the qualified Loom source.",
    },
)

LoomCorpusInfo = provider(
    doc = "Collected outputs and source identities for a Loom corpus.",
    fields = {
        "artifacts": "Depset of deployable artifacts across programs and profiles.",
        "compile_reports": "Depset of detailed compile reports.",
        "qualification_results": "Depset of batched diagnostic-xfail results.",
        "sources": "Depset of qualified Loom sources.",
    },
)

def _collect_target_profiles(targets):
    profiles = []
    seen_labels = {}
    for target in targets:
        target_profiles = (
            [target] if LoomTargetProfileInfo in target else target[LoomTargetSetInfo].profiles
        )
        for profile in target_profiles:
            label = str(profile.label)
            if label not in seen_labels:
                seen_labels[label] = None
                profiles.append(profile)
    return profiles

def _profile_stem(profile):
    info = profile[LoomTargetProfileInfo]
    return (info.family + "-" + info.selector).replace(":", "-").replace("/", "-")

def _decode_xfails(encoded_xfails, owner):
    xfails = json.decode(encoded_xfails)
    if type(xfails) != "dict":
        fail("%s has malformed diagnostic xfails" % owner)
    for root, diagnostic in xfails.items():
        if not root.startswith("@"):
            fail("%s diagnostic xfail root %r must begin with '@'" % (owner, root))
        if type(diagnostic) != "string" or not diagnostic:
            fail("%s diagnostic xfail %s must name a diagnostic" % (owner, root))
    return xfails

def _declare_subject_module(ctx, source):
    return loom_linking.declare_test_module(
        ctx = ctx,
        root_module = source,
        dependency_infos = [],
        output_stem = ctx.label.name + "/subjects",
        mnemonic = "LoomCorpusLink",
        progress_message = "Selecting tested corpus subjects from %s" % source.short_path,
        strip_check = True,
    )

def _declare_positive_compile(ctx, source, product, profile, xfails):
    profile_info = profile[LoomTargetProfileInfo]
    output_stem = ctx.label.name + "/" + _profile_stem(profile)
    artifact = ctx.actions.declare_file(output_stem + ".artifact")
    compile_report = ctx.actions.declare_file(output_stem + ".compile.json")
    args = ctx.actions.args()
    args.add(source)
    args.add("--product=%s" % product)
    args.add("--target=%s:%s" % (profile_info.family, profile_info.selector))
    for root in sorted(xfails):
        args.add("--exclude-root=%s" % root)
    args.add("--output=%s" % artifact.path)
    args.add("--compile-report=details")
    args.add("--compile-report-output=%s" % compile_report.path)

    tool = ctx.toolchains[_LOOM_COMPILE_TOOLCHAIN_TYPE].tool
    ctx.actions.run(
        arguments = [args],
        executable = tool.files_to_run,
        inputs = [source],
        mnemonic = "LoomCorpusCompile",
        outputs = [artifact, compile_report],
        progress_message = "Compiling corpus program %s for %s" % (
            source.short_path,
            profile.label,
        ),
    )
    return artifact, compile_report

def _declare_xfail_probes(ctx, source, product, profile, xfails):
    profile_info = profile[LoomTargetProfileInfo]
    output_stem = ctx.label.name + "/" + _profile_stem(profile)
    result = ctx.actions.declare_file(output_stem + ".xfails")
    compile_tool = ctx.toolchains[_LOOM_COMPILE_TOOLCHAIN_TYPE].tool
    args = ctx.actions.args()
    args.add("--compiler=%s" % compile_tool.executable.path)
    args.add("--stamp-output=%s" % result.path)
    for root in sorted(xfails):
        args.add("--expected-root=%s" % root)
        args.add("--expected-diagnostic=%s" % xfails[root])
    args.add(source)
    args.add("--product=%s" % product)
    args.add("--target=%s:%s" % (profile_info.family, profile_info.selector))
    ctx.actions.run(
        arguments = [args],
        executable = ctx.executable._xfails_tool,
        inputs = [source],
        mnemonic = "LoomCorpusXfails",
        outputs = [result],
        progress_message = "Probing corpus diagnostic xfails in %s for %s" % (
            source.short_path,
            profile.label,
        ),
        tools = [compile_tool.files_to_run],
    )
    return result

def _loom_corpus_program_impl(ctx):
    if ctx.attr.product != "module":
        fail(
            "%s has unsupported corpus product %r; this rule currently supports only 'module'" %
            (ctx.label, ctx.attr.product),
        )

    profiles = _collect_target_profiles(ctx.attr.targets)
    if not profiles:
        fail("%s must select at least one target profile" % ctx.label)
    selected_labels = {str(profile.label): None for profile in profiles}
    labels_by_output_stem = {}
    for profile in profiles:
        output_stem = _profile_stem(profile)
        prior_label = labels_by_output_stem.get(output_stem)
        if prior_label != None:
            fail(
                "%s target profiles %s and %s collide at output identity %r" %
                (ctx.label, prior_label, profile.label, output_stem),
            )
        labels_by_output_stem[output_stem] = profile.label

    xfails_by_label = {}
    for profile, encoded_xfails in ctx.attr.xfails.items():
        label = str(profile.label)
        if LoomTargetProfileInfo not in profile:
            fail("%s diagnostic xfail key %s is not a target profile" % (ctx.label, label))
        if label not in selected_labels:
            fail("%s has diagnostic xfails for unselected profile %s" % (ctx.label, label))
        xfails_by_label[label] = _decode_xfails(encoded_xfails, ctx.label)

    excludes_by_label = {}
    for profile, reason in ctx.attr.excludes.items():
        label = str(profile.label)
        if LoomTargetProfileInfo not in profile:
            fail("%s exclusion key %s is not a target profile" % (ctx.label, label))
        if label not in selected_labels:
            fail("%s has an exclusion for unselected profile %s" % (ctx.label, label))
        if not reason:
            fail("%s exclusion for %s must include a reason" % (ctx.label, label))
        if label in xfails_by_label:
            fail("%s cannot both exclude and xfail profile %s" % (ctx.label, label))
        excludes_by_label[label] = reason

    subject_module = _declare_subject_module(ctx, ctx.file.src)
    artifacts = []
    compile_reports = []
    qualification_results = []
    for profile in profiles:
        label = str(profile.label)
        if label in excludes_by_label:
            continue
        xfails = xfails_by_label.get(label, {})
        artifact, compile_report = _declare_positive_compile(
            ctx,
            subject_module,
            ctx.attr.product,
            profile,
            xfails,
        )
        artifacts.append(artifact)
        compile_reports.append(compile_report)
        if xfails:
            qualification_results.append(_declare_xfail_probes(
                ctx,
                subject_module,
                ctx.attr.product,
                profile,
                xfails,
            ))

    if not artifacts and not qualification_results:
        fail("%s excludes every selected target profile" % ctx.label)

    artifacts_depset = depset(artifacts)
    compile_reports_depset = depset(compile_reports)
    qualification_results_depset = depset(qualification_results)
    default_files = depset(
        transitive = [
            artifacts_depset,
            compile_reports_depset,
            qualification_results_depset,
        ],
    )
    return [
        DefaultInfo(files = default_files),
        OutputGroupInfo(
            artifacts = artifacts_depset,
            compile_reports = compile_reports_depset,
            xfail_results = qualification_results_depset,
        ),
        _LoomCorpusProgramInfo(
            artifacts = artifacts_depset,
            compile_reports = compile_reports_depset,
            qualification_results = qualification_results_depset,
            sources = depset([ctx.file.src]),
        ),
    ]

_loom_corpus_program = rule(
    implementation = _loom_corpus_program_impl,
    attrs = {
        "excludes": attr.label_keyed_string_dict(
            doc = "Concrete target profiles mapped to whole-source exclusion reasons.",
        ),
        "product": attr.string(
            mandatory = True,
            doc = "Explicit deployable product owned by the source program.",
        ),
        "src": attr.label(
            allow_single_file = [".loom"],
            mandatory = True,
            doc = "One independently compiled corpus source program.",
        ),
        "targets": attr.label_list(
            mandatory = True,
            providers = [
                [LoomTargetProfileInfo],
                [LoomTargetSetInfo],
            ],
            doc = "Target profiles or centrally maintained target sets.",
        ),
        "xfails": attr.label_keyed_string_dict(
            doc = "Concrete target profiles mapped to encoded root diagnostics.",
        ),
        "_xfails_tool": attr.label(
            cfg = "exec",
            default = Label("//loom/build_tools/corpus:loom-corpus-compile-xfails"),
            executable = True,
        ),
    },
    doc = "Selects tested subjects and compiles them independently for every profile.",
    toolchains = [
        _LOOM_COMPILE_TOOLCHAIN_TYPE,
        _LOOM_LINK_TOOLCHAIN_TYPE,
    ],
)

def _loom_corpus_aggregate_impl(ctx):
    artifacts = depset(transitive = [
        dep[_LoomCorpusProgramInfo].artifacts
        for dep in ctx.attr.programs
    ])
    compile_reports = depset(transitive = [
        dep[_LoomCorpusProgramInfo].compile_reports
        for dep in ctx.attr.programs
    ])
    qualification_results = depset(transitive = [
        dep[_LoomCorpusProgramInfo].qualification_results
        for dep in ctx.attr.programs
    ])
    sources = depset(transitive = [
        dep[_LoomCorpusProgramInfo].sources
        for dep in ctx.attr.programs
    ])
    return [
        DefaultInfo(files = depset(transitive = [
            artifacts,
            compile_reports,
            qualification_results,
        ])),
        OutputGroupInfo(
            artifacts = artifacts,
            compile_reports = compile_reports,
            xfail_results = qualification_results,
        ),
        LoomCorpusInfo(
            artifacts = artifacts,
            compile_reports = compile_reports,
            qualification_results = qualification_results,
            sources = sources,
        ),
    ]

_loom_corpus_aggregate = rule(
    implementation = _loom_corpus_aggregate_impl,
    attrs = {
        "programs": attr.label_list(
            mandatory = True,
            providers = [_LoomCorpusProgramInfo],
        ),
    },
    doc = "Collects independently cacheable corpus program outputs.",
)

def _program_target_name(corpus_name, source):
    source_stem = source[:-len(".loom")]
    source_stem = source_stem.replace("/", "_").replace(".", "_")
    return corpus_name + "_" + source_stem

def _partition_exceptions(srcs, exceptions, kind):
    by_source = {src: {} for src in srcs}
    for profile, entries in exceptions.items():
        if type(entries) != "dict":
            fail("loom_corpus %s entries for %s must be a dictionary" % (kind, profile))
        for identity, value in entries.items():
            if kind == "xfail":
                separator = identity.find(":@")
                if separator == -1:
                    fail(
                        "loom_corpus xfail identity %r must use '<source>:@<root>'" %
                        identity,
                    )
                source = identity[:separator]
                key = identity[separator + 1:]
            else:
                source = identity
                key = identity
            if source not in by_source:
                fail("loom_corpus %s names unknown source %r" % (kind, source))
            profile_entries = by_source[source].setdefault(profile, {})
            profile_entries[key] = value
    return by_source

def loom_corpus(
        name,
        srcs,
        targets,
        products,
        xfails = {},
        excludes = {},
        **kwargs):
    """Declares exhaustive build qualification for a singular corpus package.

    Args:
      name: Aggregate build target name.
      srcs: Complete explicit inventory of .loom files in this package. Each
        source owns tests that select its deployable callable subjects.
      targets: Central target-set or concrete target-profile labels.
      products: Dictionary mapping every source to its explicit product kind.
      xfails: Target-profile keyed dictionaries mapping '<source>:@<root>' to
        canonical diagnostics.
      excludes: Target-profile keyed dictionaries mapping sources to reasons.
      **kwargs: Common rule attributes applied to the aggregate and programs.
    """
    srcs = iree_checked_glob(
        files = srcs,
        include = ["*.loom"],
        allow_empty = False,
    )
    if not targets:
        fail("loom_corpus requires at least one target or target set")
    if sorted(products.keys()) != sorted(srcs):
        fail(
            "loom_corpus products must account for every source exactly; got %r for %r" %
            (sorted(products.keys()), sorted(srcs)),
        )

    xfails_by_source = _partition_exceptions(srcs, xfails, "xfail")
    excludes_by_source = _partition_exceptions(srcs, excludes, "exclusion")
    program_names = []
    for source in srcs:
        program_name = _program_target_name(name, source)
        if program_name in program_names:
            fail("loom_corpus source names collide at generated target %r" % program_name)
        program_names.append(program_name)

        source_xfails = {
            profile: json.encode(entries)
            for profile, entries in xfails_by_source[source].items()
        }
        source_excludes = {}
        for profile, entries in excludes_by_source[source].items():
            reason = entries[source]
            if not reason:
                fail(
                    "loom_corpus exclusion for %s on %s must include a reason" %
                    (source, profile),
                )
            if profile in source_xfails:
                fail(
                    "loom_corpus source %s cannot both exclude and xfail profile %s" %
                    (source, profile),
                )
            source_excludes[profile] = reason

        _loom_corpus_program(
            name = program_name,
            excludes = source_excludes,
            product = products[source],
            src = source,
            targets = targets,
            xfails = source_xfails,
            **kwargs
        )

    _loom_corpus_aggregate(
        name = name,
        programs = [":" + program_name for program_name in program_names],
        **kwargs
    )
