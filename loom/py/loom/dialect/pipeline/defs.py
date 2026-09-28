# Copyright 2026 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Portable persistent-pipeline type and operation definitions."""

from loom.assembly import (
    ARROW,
    COLON,
    COMMA,
    GLUE,
    LBRACKET,
    LPAREN,
    RBRACKET,
    RPAREN,
    Attr,
    FuncArgs,
    IndexList,
    OptionalGroup,
    Param,
    PredicateList,
    Ref,
    Refs,
    Region,
    ResultType,
    ResultTypeList,
    Scope,
    SymbolRef,
    TemplateParam,
    TemplateParamFlags,
    TypeOf,
    TypesOf,
    kw,
)
from loom.dialect.combining import CombiningKind
from loom.dialect.func.defs import Retain, Visibility
from loom.dialect.scalar import FastMathFlags
from loom.dsl import (
    ANY,
    ATTR_TYPE_ENUM,
    ATTR_TYPE_FLAGS,
    ATTR_TYPE_I64,
    ATTR_TYPE_I64_ARRAY,
    INDEX,
    ISOLATED_FROM_ABOVE,
    PURE,
    SYMBOL_DEFINE,
    TERMINATOR,
    UNKNOWN_EFFECTS,
    VIEW,
    AttrDef,
    CallLikeInterface,
    CallLikeKind,
    Dialect,
    EnumCase,
    EnumDef,
    FuncLikeInterface,
    HasAncestor,
    HasParent,
    ImplicitTerminator,
    Op,
    Operand,
    OpPhase,
    RegionDef,
    Result,
    SameType,
    SymbolDefinition,
    SymbolReference,
    TypeDef,
)

pipeline_ops = Dialect(
    "pipeline",
    dialect_id=0x23,
    doc="Portable persistent dataflow programs over typed record streams.",
)

pipeline_flow_type = TypeDef(
    "pipeline.flow",
    params=[AttrDef("element_type", "type")],
    format=[Param("element_type")],
    doc=("Typed ordered record stream between scheduling groups. The element type describes one transferred value and is normally tile<...>."),
)

PipelineScope = EnumDef(
    "PipelineScope",
    [
        EnumCase(
            "kernel",
            1,
            doc="Must materialize as one directly loadable executable artifact.",
        ),
    ],
    doc=("Required materialization boundary. An absent scope permits a generic pipeline program that may span targets and runtime operations."),
)

_PIPELINE_MODIFIER_FORMAT = [
    OptionalGroup([TemplateParam("scope")], anchor="scope"),
    OptionalGroup([Attr("visibility")], anchor="visibility"),
    OptionalGroup([Attr("retain")], anchor="retain"),
    OptionalGroup(
        [kw("target"), GLUE, LPAREN, SymbolRef("target"), GLUE, RPAREN],
        anchor="target",
    ),
]

_PIPELINE_SIGNATURE_FORMAT = [
    SymbolRef("callee"),
    Scope(
        [
            FuncArgs(
                "args",
                group="specializations",
                end_attr="specialization_count",
            ),
            kw("run"),
            FuncArgs(
                "args",
                group="bindings",
                start_attr="specialization_count",
            ),
            OptionalGroup(
                [kw("where"), PredicateList("predicates")],
                anchor="predicates",
            ),
        ]
    ),
]

_PIPELINE_ATTRS = [
    AttrDef("callee", "symbol"),
    AttrDef("scope", ATTR_TYPE_ENUM, enum_def=PipelineScope, optional=True),
    AttrDef("visibility", "enum", enum_def=Visibility, optional=True),
    AttrDef("retain", "enum", enum_def=Retain, optional=True),
    AttrDef(
        "target",
        "symbol",
        optional=True,
        symbol_ref=SymbolReference("target", ["target"]),
    ),
    AttrDef("predicates", "predicate_list", optional=True),
    AttrDef(
        "specialization_count",
        ATTR_TYPE_I64,
        doc="Number of leading staged pipeline arguments.",
    ),
]

pipeline_def = Op(
    "pipeline.def",
    group=pipeline_ops,
    phase=OpPhase.EXECUTABLE,
    doc=(
        "Persistent dataflow program. Leading specialization arguments remain "
        "ordinary SSA values and typed run arguments are supplied by each "
        "invocation. Their source types are independent of the eventual target "
        "export ABI. The optional scope fixes the artifact boundary that "
        "lowering must satisfy."
    ),
    traits=[SYMBOL_DEFINE, ISOLATED_FROM_ABOVE, ImplicitTerminator("pipeline.finish")],
    attrs=list(_PIPELINE_ATTRS),
    symbol_def=SymbolDefinition(
        field="callee",
        name="pipeline",
        interfaces=["func_like", "pipeline"],
        bytecode_kind="LOOM_SYMBOL_FUNC_DEF",
        fact_domain="loom_func_symbol_fact_domain",
        retain="retain",
        product_carrier="scope",
    ),
    regions=[
        RegionDef(
            "body",
            doc="Construction of invocation-owned storage and independently progressing strands.",
            terminator="pipeline.finish",
            buffer_arg_memory_space="global",
        )
    ],
    interfaces=[
        FuncLikeInterface(
            callee="callee",
            visibility="visibility",
            target="target",
            predicates="predicates",
            specialization_count="specialization_count",
            body="body",
        )
    ],
    verify="loom_pipeline_def_verify",
    format=[
        *_PIPELINE_MODIFIER_FORMAT,
        *_PIPELINE_SIGNATURE_FORMAT,
        Region("body"),
    ],
    examples=[
        "pipeline.def<kernel> target(@array) @resident() run(%input: buffer, %output: buffer) {\n}",
        "pipeline.def @heterogeneous(%batch: index) run(%input: buffer) {\n}",
    ],
)

_PIPELINE_GRAPH_TRAITS = [HasAncestor("pipeline.def")]

pipeline_compose = Op(
    "pipeline.compose",
    group=pipeline_ops,
    doc=(
        "Compose a child pipeline into the enclosing invocation. Each composition "
        "constructs fresh child storage and independently progressing strands; "
        "it neither submits another invocation nor waits for child completion. "
        "Specialization and run operands substitute the child's complete typed "
        "signature. Child target and materialization requirements remain binding."
    ),
    operands=[
        Operand("specializations", ANY, variadic=True),
        Operand("bindings", ANY, variadic=True),
    ],
    attrs=[
        AttrDef(
            "callee",
            "symbol",
            symbol_ref=SymbolReference("pipeline", ["pipeline"]),
        ),
    ],
    traits=[UNKNOWN_EFFECTS, *_PIPELINE_GRAPH_TRAITS],
    interfaces=[
        CallLikeInterface(
            callee="callee",
            operands="specializations",
            results=None,
            kind=CallLikeKind.COMPOSITION,
        ),
    ],
    verify="loom_pipeline_compose_verify",
    format=[
        SymbolRef("callee"),
        OptionalGroup(
            [GLUE, LBRACKET, Refs("specializations"), RBRACKET],
            anchor="specializations",
        ),
        GLUE,
        LPAREN,
        Refs("bindings"),
        RPAREN,
        COLON,
        OptionalGroup(
            [LBRACKET, TypesOf("specializations"), RBRACKET, GLUE],
            anchor="specializations",
        ),
        LPAREN,
        TypesOf("bindings"),
        RPAREN,
    ],
    examples=[
        "pipeline.compose @column[%column](%input, %output) : [index](channel<tile<16xi32>>, channel<tile<16xi32>>)",
    ],
)

pipeline_strand = Op(
    "pipeline.strand",
    group=pipeline_ops,
    doc=(
        "Construct independently progressing strand instances over a target-relative "
        "worker domain. Each selected worker enters its instance once; ordinary "
        "loops in the body express repeated work. The region captures lexical SSA "
        "values and declares work rather than executing inline. Multiple strands "
        "may share a worker, with channel dependencies controlling their progress. "
        "Origins, counts and strides select coordinates as origin + index * stride. "
        "An omitted target inherits the enclosing pipeline's target environment."
    ),
    operands=[
        Operand("origins", INDEX, variadic=True, doc="Dynamic worker origins."),
        Operand("counts", INDEX, variadic=True, doc="Dynamic worker counts."),
        Operand("strides", INDEX, variadic=True, doc="Dynamic worker strides."),
    ],
    attrs=[
        AttrDef(
            "target",
            "symbol",
            optional=True,
            symbol_ref=SymbolReference("target", ["target"]),
        ),
        AttrDef(
            "static_origins",
            ATTR_TYPE_I64_ARRAY,
            doc="Worker origins; INT64_MIN entries refer to dynamic origins.",
        ),
        AttrDef(
            "static_counts",
            ATTR_TYPE_I64_ARRAY,
            doc="Worker counts; INT64_MIN entries refer to dynamic counts.",
        ),
        AttrDef(
            "static_strides",
            ATTR_TYPE_I64_ARRAY,
            doc="Worker strides; INT64_MIN entries refer to dynamic strides.",
        ),
    ],
    regions=[
        RegionDef(
            "body",
            doc="One complete strand instance, with lexical captures.",
            terminator="pipeline.end",
        ),
    ],
    traits=[
        UNKNOWN_EFFECTS,
        *_PIPELINE_GRAPH_TRAITS,
        ImplicitTerminator("pipeline.end"),
    ],
    verify="loom_pipeline_strand_verify",
    format=[
        OptionalGroup(
            [kw("target"), GLUE, LPAREN, SymbolRef("target"), GLUE, RPAREN],
            anchor="target",
        ),
        kw("workers"),
        GLUE,
        LPAREN,
        IndexList("origins", "static_origins"),
        COMMA,
        IndexList("counts", "static_counts", glue=False),
        COMMA,
        IndexList("strides", "static_strides", glue=False),
        GLUE,
        RPAREN,
        Region("body"),
    ],
    examples=["pipeline.strand target(@npu) workers([%column, 2], [1, 1], [1, 1]) {\n}"],
)

pipeline_end = Op(
    "pipeline.end",
    group=pipeline_ops,
    doc="Complete a strand instance. Implicit at the end of its region.",
    traits=[TERMINATOR, HasParent("pipeline.strand")],
    examples=["pipeline.end"],
)

pipeline_scatter = Op(
    "pipeline.scatter",
    group=pipeline_ops,
    doc=(
        "Partition the leading dimension of a source view across a scheduling "
        "group. The flow tile matches a trailing record suffix; intervening "
        "dimensions form a finite ordered record sequence for each lane."
    ),
    operands=[
        Operand("source", VIEW, doc="Dense source view with a lane dimension."),
        Operand("group", ANY, doc="Destination scheduling group."),
    ],
    results=[Result("result", ANY, doc="Per-lane tile flow.")],
    traits=[PURE, *_PIPELINE_GRAPH_TRAITS],
    verify="loom_pipeline_scatter_verify",
    format=[
        Ref("source"),
        kw("across"),
        Ref("group"),
        COLON,
        TypeOf("source"),
        COMMA,
        TypeOf("group"),
        ARROW,
        ResultType("result"),
    ],
    examples=["%tiles = pipeline.scatter %source across %workers : view<2x8x8xi8>, group<2> -> pipeline.flow<tile<8x8xi8>>"],
)

pipeline_read = Op(
    "pipeline.read",
    group=pipeline_ops,
    doc=("Read the same finite source-view record sequence for each destination lane. The flow tile matches a trailing record suffix and preceding dimensions form the ordered sequence."),
    operands=[
        Operand("source", VIEW, doc="Source view containing one tile record."),
        Operand("group", ANY, doc="Destination scheduling group."),
    ],
    results=[Result("result", ANY, doc="Tile flow delivered to the group.")],
    traits=[PURE, *_PIPELINE_GRAPH_TRAITS],
    verify="loom_pipeline_read_verify",
    format=[
        Ref("source"),
        kw("on"),
        Ref("group"),
        COLON,
        TypeOf("source"),
        COMMA,
        TypeOf("group"),
        ARROW,
        ResultType("result"),
    ],
    examples=["%bias = pipeline.read %source on %reducers : view<8x8xi32>, group<1> -> pipeline.flow<tile<8x8xi32>>"],
)

pipeline_stage = Op(
    "pipeline.stage",
    group=pipeline_ops,
    doc=("Instantiate one stage invocation per group lane. Inputs and outputs are lane-wise typed flows; the referenced callable supplies one record-firing implementation."),
    operands=[
        Operand("group", ANY, doc="Scheduling group executing the stage."),
        Operand("inputs", ANY, variadic=True, doc="Lane-wise input flows."),
    ],
    results=[Result("outputs", ANY, variadic=True, doc="Lane-wise output flows.")],
    attrs=[
        AttrDef(
            "entry",
            "symbol",
            symbol_ref=SymbolReference("function", ["callable"]),
        )
    ],
    traits=[UNKNOWN_EFFECTS, *_PIPELINE_GRAPH_TRAITS],
    verify="loom_pipeline_stage_verify",
    format=[
        SymbolRef("entry"),
        kw("on"),
        Ref("group"),
        GLUE,
        LPAREN,
        Refs("inputs"),
        RPAREN,
        COLON,
        LPAREN,
        TypeOf("group"),
        OptionalGroup([COMMA, TypesOf("inputs")], anchor="inputs"),
        RPAREN,
        ARROW,
        ResultTypeList("outputs"),
    ],
    examples=["%partials = pipeline.stage @product on %workers(%lhs, %rhs) : (group<2>, pipeline.flow<tile<8x8xi8>>, pipeline.flow<tile<8x8xi8>>) -> (pipeline.flow<tile<8x8xi32>>)"],
)

pipeline_buffer = Op(
    "pipeline.buffer",
    group=pipeline_ops,
    doc=("Require an independently buffered flow with SSA-defined minimum record capacity. Targets may select a greater capacity."),
    operands=[
        Operand("source", ANY, doc="Input tile flow."),
        Operand("capacity", INDEX, doc="Required minimum record capacity."),
    ],
    results=[
        Result(
            "result",
            ANY,
            allocates=True,
            doc="Distinct buffered tile flow.",
        )
    ],
    constraints=[SameType("source", "result")],
    traits=[*_PIPELINE_GRAPH_TRAITS],
    verify="loom_pipeline_buffer_verify",
    format=[
        Ref("source"),
        kw("capacity"),
        Ref("capacity"),
        COLON,
        LPAREN,
        TypeOf("source"),
        COMMA,
        TypeOf("capacity"),
        RPAREN,
        ARROW,
        ResultType("result"),
    ],
    examples=["%buffered = pipeline.buffer %partials capacity %ring_capacity : (pipeline.flow<tile<8x8xi32>>, index) -> pipeline.flow<tile<8x8xi32>>"],
)

pipeline_fold = Op(
    "pipeline.fold",
    group=pipeline_ops,
    doc=(
        "Fold the innermost temporal dimension of each lane's finite input "
        "record sequence using the template combining kind. Outer temporal "
        "dimensions, lane cardinality, and tile shape are preserved. Optional "
        "fastmath flags permit the corresponding floating-point reassociation "
        "and approximation choices."
    ),
    operands=[Operand("source", ANY, doc="Finite per-lane input record flow.")],
    results=[
        Result(
            "result",
            ANY,
            doc="One folded record per outer temporal position and source lane.",
        )
    ],
    attrs=[
        AttrDef("kind", ATTR_TYPE_ENUM, enum_def=CombiningKind),
        AttrDef(
            "fastmath",
            ATTR_TYPE_FLAGS,
            optional=True,
            enum_def=FastMathFlags,
        ),
    ],
    constraints=[SameType("source", "result")],
    traits=[UNKNOWN_EFFECTS, *_PIPELINE_GRAPH_TRAITS],
    verify="loom_pipeline_fold_verify",
    format=[
        TemplateParamFlags("kind", "fastmath"),
        Ref("source"),
        COLON,
        TypeOf("source"),
    ],
    examples=["%partial = pipeline.fold<addf, reassoc> %contributions : pipeline.flow<tile<1xf32>>"],
)

pipeline_reduce = Op(
    "pipeline.reduce",
    group=pipeline_ops,
    doc=("Gather each source-group lane record into one target-group stage firing. Target inputs remain pointwise with the target group."),
    operands=[
        Operand("source_group", ANY, doc="Group whose lane records are gathered."),
        Operand("source_inputs", ANY, variadic=True, doc="Gathered source-group flows."),
        Operand("target_group", ANY, doc="Group executing the reduction stage."),
        Operand("target_inputs", ANY, variadic=True, doc="Pointwise target-group flows."),
    ],
    results=[Result("outputs", ANY, variadic=True, doc="Target-group output flows.")],
    attrs=[
        AttrDef(
            "entry",
            "symbol",
            symbol_ref=SymbolReference("function", ["callable"]),
        )
    ],
    traits=[UNKNOWN_EFFECTS, *_PIPELINE_GRAPH_TRAITS],
    verify="loom_pipeline_reduce_verify",
    format=[
        SymbolRef("entry"),
        kw("from"),
        Ref("source_group"),
        GLUE,
        LPAREN,
        Refs("source_inputs"),
        RPAREN,
        kw("to"),
        Ref("target_group"),
        GLUE,
        LPAREN,
        Refs("target_inputs"),
        RPAREN,
        COLON,
        LPAREN,
        TypeOf("source_group"),
        OptionalGroup([COMMA, TypesOf("source_inputs")], anchor="source_inputs"),
        RPAREN,
        kw("to"),
        LPAREN,
        TypeOf("target_group"),
        OptionalGroup([COMMA, TypesOf("target_inputs")], anchor="target_inputs"),
        RPAREN,
        ARROW,
        ResultTypeList("outputs"),
    ],
    examples=[
        "%result = pipeline.reduce @sum from %products(%partials) to %reducers(%bias) : (group<2>, pipeline.flow<tile<8x8xi32>>) to (group<1>, pipeline.flow<tile<8x8xi32>>) -> (pipeline.flow<tile<8x8xi32>>)"
    ],
)

pipeline_write = Op(
    "pipeline.write",
    group=pipeline_ops,
    doc=(
        "Write each source-group record sequence to a destination view. "
        "The view contains a leading group-lane dimension, the exact ordered "
        "temporal record dimensions, and the trailing flow tile dimensions. "
        "A single-lane group may omit the leading lane dimension. Unit temporal "
        "dimensions remain part of the record shape."
    ),
    operands=[
        Operand("source", ANY, doc="Source tile flow."),
        Operand("target", VIEW, doc="Destination view."),
    ],
    traits=[UNKNOWN_EFFECTS, *_PIPELINE_GRAPH_TRAITS],
    verify="loom_pipeline_write_verify",
    format=[
        Ref("source"),
        kw("to"),
        Ref("target"),
        COLON,
        TypeOf("source"),
        COMMA,
        TypeOf("target"),
    ],
    examples=["pipeline.write %result to %output : pipeline.flow<tile<8x8xi32>>, view<8x8xi32>"],
)

pipeline_finish = Op(
    "pipeline.finish",
    group=pipeline_ops,
    doc=("Complete the pipeline body. Invocation completion follows the execution and communication obligations of the materialized program."),
    traits=[TERMINATOR, HasParent("pipeline.def")],
    examples=["pipeline.finish"],
)

ALL_PIPELINE_TYPES: tuple[TypeDef, ...] = (pipeline_flow_type,)
ALL_PIPELINE_OPS: tuple[Op, ...] = (
    pipeline_def,
    pipeline_scatter,
    pipeline_read,
    pipeline_stage,
    pipeline_buffer,
    pipeline_fold,
    pipeline_reduce,
    pipeline_write,
    pipeline_finish,
    pipeline_strand,
    pipeline_end,
    pipeline_compose,
)
