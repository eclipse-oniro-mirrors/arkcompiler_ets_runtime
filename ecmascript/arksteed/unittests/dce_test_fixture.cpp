/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ecmascript/arksteed/unittests/dce_test_fixture.h"
#include "ecmascript/frames.h"
#include "ecmascript/js_array.h"
#include "securec.h"

namespace panda::ecmascript::arksteed {

std::string SafeOpcodeNameForTest(VertexOpcode opcode)
{
    return opcode >= FIRST_OPCODE && opcode <= LAST_OPCODE ? OpcodeToString(opcode) : "<invalid-opcode>";
}

void SetOpcodeForTest(Vertex *vertex, VertexOpcode opcode)
{
    uint64_t bits = 0;
    ASSERT_EQ(memcpy_s(&bits, sizeof(bits), vertex, sizeof(bits)), EOK);
    bits = Vertex::OpcodeField::Update(bits, opcode);
    ASSERT_EQ(memcpy_s(vertex, sizeof(Vertex), &bits, sizeof(bits)), EOK);
}

void SetPropertyFlagsForTest(Vertex *vertex, VertexPropertyFlag flags)
{
    uint64_t bits = 0;
    ASSERT_EQ(memcpy_s(&bits, sizeof(bits), vertex, sizeof(bits)), EOK);
    bits = Vertex::PropertyFlagsField::Update(bits, static_cast<uint32_t>(flags));
    ASSERT_EQ(memcpy_s(vertex, sizeof(Vertex), &bits, sizeof(bits)), EOK);
}

std::string DescribeHiddenReferences(const Vertex *vertex)
{
    std::ostringstream out;
    if (const EagerDeoptimizableMixin *eager = EagerDeoptimizableMixinOf(vertex); eager != nullptr) {
        out << ",eager" << eager->GetDeoptFrameValueCount() << ",eagerBc=" << eager->GetBytecodeOffset() << "[";
        for (uint32_t index = 0, count = eager->GetDeoptFrameValueCount(); index < count; ++index) {
            out << static_cast<const void *>(eager->GetDeoptFrameValue(index)) <<
                ":vreg" << eager->GetDeoptVReg(index) << ":kind" <<
                static_cast<int>(eager->GetDeoptValueKind(index)) << ",";
        }
        out << "]";
    } else {
        out << ",eager-";
    }
    if (const LazyDeoptimizableMixin *lazy = LazyDeoptimizableMixinOf(vertex); lazy != nullptr) {
        out << ",lazy" << (lazy->HasLazyDeoptFrameState() ? 1 : 0);
        if (lazy->HasLazyDeoptFrameState()) {
            out << ",lazyBc=" << lazy->GetBytecodeOffset();
        }
        out << "[";
        for (uint32_t index = 0, count = lazy->GetDeoptFrameValueCount(); index < count; ++index) {
            out << static_cast<const void *>(lazy->GetDeoptFrameValue(index)) <<
                ":vreg" << lazy->GetDeoptVReg(index) << ":kind" <<
                static_cast<int>(lazy->GetDeoptValueKind(index)) << ",";
        }
        out << "]";
    } else {
        out << ",lazy-";
    }
    if (const ThrowableMixin *throwable = ThrowableMixinOf(vertex); throwable != nullptr) {
        out << ",catch=" << static_cast<const void *>(throwable->GetCatchBlock()) <<
            ",catchIndex=" << throwable->GetStoredCatchPredecessorIndex() <<
            ",excLazy=" << (throwable->HasExceptionLazyDeopt() ? 1 : 0);
    } else {
        out << ",no-catch";
    }
    return out.str();
}

std::string DescribeConcreteOptions(const Vertex *vertex)
{
    std::ostringstream out;
    if (const auto *branch = vertex->TryCast<BranchIfInt32CompareVertex>(); branch != nullptr) {
        out << ",cond=" << static_cast<int>(branch->GetCondition());
    } else if (const auto *check = vertex->TryCast<I32ConditionCheckVertex>(); check != nullptr) {
        out << ",cond=" << static_cast<int>(check->GetCondition());
    } else if (const auto *check = vertex->TryCast<F64ConditionCheckVertex>(); check != nullptr) {
        out << ",cond=" << static_cast<int>(check->GetCondition());
    } else if (const auto *check = vertex->TryCast<Float64ToBooleanVertex>(); check != nullptr) {
        out << ",nonZero=" << (check->IsTrueIfNonZero() ? 1 : 0);
    } else if (const auto *deopt = vertex->TryCast<DeoptIfReferenceNotEqualVertex>(); deopt != nullptr) {
        out << ",deoptType=" << static_cast<int>(deopt->GetDeoptType());
    } else if (const auto *deopt = vertex->TryCast<DeoptIfTaggedConditionVertex>(); deopt != nullptr) {
        out << ",deoptType=" << static_cast<int>(deopt->GetDeoptType()) <<
            ",cond=" << static_cast<int>(deopt->GetCondition());
    } else if (const auto *hclass = vertex->TryCast<DeoptIfHClassNotInVertex>(); hclass != nullptr) {
        out << ",hclassCount=" << hclass->GetExpectedHClassCount();
    } else if (const auto *hclass = vertex->TryCast<BranchIfHClassInVertex>(); hclass != nullptr) {
        out << ",hclassCount=" << hclass->GetExpectedHClassCount() <<
            ",addr=" << (hclass->InputIsHClassAddress() ? 1 : 0);
    } else if (const auto *builtin = vertex->TryCast<CallKnownBuiltinVertex>(); builtin != nullptr) {
        out << ",builtin=" << static_cast<int>(builtin->GetBuiltinId()) <<
            ",entry=" << static_cast<int>(builtin->GetEntryKind());
    } else if (const auto *divide = vertex->TryCast<I32DivByConstWithCheckVertex>(); divide != nullptr) {
        out << ",divisor=" << divide->GetDivisor();
    } else if (const auto *ctor = vertex->TryCast<CallKnownJSFunctionVertex>(); ctor != nullptr) {
        out << ",argc=" << ctor->GetActualArgc();
    } else if (const auto *call = vertex->TryCast<CallVertex>(); call != nullptr) {
        out << ",argc=" << call->GetActualArgc();
    }
    return out.str();
}

std::string DescribeVertexRecord(const Vertex *vertex)
{
    if (vertex == nullptr) {
        return "null";
    }
    std::ostringstream out;
    out << SafeOpcodeNameForTest(vertex->GetOpcode()) << "@" << static_cast<const void *>(vertex) <<
        "(repr" << static_cast<int>(vertex->GetValueRepresentation()) << ",owner" <<
        static_cast<const void *>(vertex->GetOwner()) << ",n" << vertex->GetInputCount() <<
        ",regalloc=" << static_cast<const void *>(vertex->GetRegallocInfo()) << ")" << "[";
    for (uint32_t index = 0; index < vertex->GetInputCount(); ++index) {
        out << static_cast<const void *>(vertex->GetInput(index)) << ",";
    }
    out << "]" << DescribeConcreteOptions(vertex) << DescribeHiddenReferences(vertex);
    return out.str();
}

std::string DescribeConstantPayload(const ValueVertex *value)
{
    if (value == nullptr) {
        return "null";
    }
    std::ostringstream out;
    out << SafeOpcodeNameForTest(value->GetOpcode());
    if (const auto *c = value->TryCast<Int32ConstantVertex>(); c != nullptr) {
        out << ":" << c->GetValue();
    } else if (const auto *c = value->TryCast<Int64ConstantVertex>(); c != nullptr) {
        out << ":" << c->GetValue();
    } else if (const auto *c = value->TryCast<Float64ConstantVertex>(); c != nullptr) {
        out << ":" << c->GetValue();
    } else if (const auto *c = value->TryCast<TaggedConstantVertex>(); c != nullptr) {
        out << ":" << c->GetValue();
    } else if (const auto *c = value->TryCast<HeapConstantVertex>(); c != nullptr) {
        out << ":" << c->GetHandleIndex();
    }
    return out.str();
}

std::string SnapshotRegistriesText(Graph *graph)
{
    std::ostringstream out;
    out << "params=" << graph->GetParameterCount();
    for (size_t index = 0, count = graph->GetParameterCount(); index < count; ++index) {
        const ValueVertex *parameter = graph->GetParameter(static_cast<uint32_t>(index));
        out << " p" << index << "=" << static_cast<const void *>(parameter);
        if (parameter != nullptr && parameter->GetOpcode() == VertexOpcode::InitialValue) {
            out << ":slot" << static_cast<const InitialValueVertex *>(parameter)->GetFrameSlotIndex();
        }
    }
    for (const auto &entry : graph->GetInt32Constants()) {
        out << " i32(" << entry.first << ")=" << static_cast<const void *>(entry.second) <<
            ":" << DescribeConstantPayload(entry.second);
    }
    for (const auto &entry : graph->GetInt64Constants()) {
        out << " i64(" << entry.first << ")=" << static_cast<const void *>(entry.second) <<
            ":" << DescribeConstantPayload(entry.second);
    }
    for (const auto &entry : graph->GetFloat64Constants()) {
        out << " f64(" << entry.first << ")=" << static_cast<const void *>(entry.second) <<
            ":" << DescribeConstantPayload(entry.second);
    }
    for (const auto &entry : graph->GetTaggedConstants()) {
        out << " tg(" << entry.first << ")=" << static_cast<const void *>(entry.second) <<
            ":" << DescribeConstantPayload(entry.second);
    }
    for (const auto &entry : graph->GetHeapConstants()) {
        out << " hp(" << entry.first << ")=" << static_cast<const void *>(entry.second) <<
            ":" << DescribeConstantPayload(entry.second);
    }
    return out.str();
}

std::string SnapshotControlText(Graph *graph)
{
    std::ostringstream out;
    for (const BB *block : *graph) {
        ControlVertex *control = block->GetControlVertex();
        out << "B" << block->GetId() << "@" << static_cast<const void *>(block) <<
            " ctrl=" << static_cast<const void *>(control) << " loop=" << (block->IsLoopHeader() ? 1 : 0) <<
            " exc=" << (block->IsExceptionHandler() ? 1 : 0) << " deferred=" << (block->IsDeferred() ? 1 : 0) <<
            " peeled=" << (block->HasPeeledIteration() ? 1 : 0) <<
            " merge=" << (block->HasRegisterMergeState() ? 1 : 0) <<
            " loopInfo=" << static_cast<const void *>(block->GetRegallocLoopInfo()) <<
            " effects=" << static_cast<const void *>(block->GetLoopEffects()) << " preds[";
        for (const BB *predecessor : block->GetPredecessors()) {
            out << static_cast<const void *>(predecessor) << ",";
        }
        out << "]";
        if (control != nullptr) {
            if (const UnconditionalControlVertex *unconditional = control->TryCast<UnconditionalControlVertex>();
                unconditional != nullptr) {
                out << " predId=" << unconditional->GetPredecessorId() << " target=" <<
                    static_cast<const void *>(unconditional->Target());
            }
            if (const BranchControlVertex *branch = control->TryCast<BranchControlVertex>(); branch != nullptr) {
                out << " ifTrue=" << static_cast<const void *>(branch->IfTrue()) << " ifFalse=" <<
                    static_cast<const void *>(branch->IfFalse());
            }
        }
        out << "\n";
    }
    return out.str();
}

std::string SnapshotGraphScalarsText(Graph *graph)
{
    std::ostringstream out;
    out << "maxBlockId=" << graph->MaxBlockId() << " blocks=" << graph->NumBlocks() <<
        " recursive=" << (graph->HasRecursiveCalls() ? 1 : 0) <<
        " unreachable=" << (graph->MayHaveUnreachableBlocks() ? 1 : 0) <<
        " maxCallStackArgs=" << graph->GetMaxCallStackArgs() <<
        " taggedSlots=" << graph->GetTaggedStackSlots() <<
        " untaggedSlots=" << graph->GetUntaggedStackSlots() <<
        " reuseSlots=" << (graph->GetReuseStackSlots() ? 1 : 0);
    return out.str();
}

std::string SnapshotGraphText(Graph *graph)
{
    std::ostringstream out;
    out << SnapshotGraphScalarsText(graph) << "\n" << SnapshotRegistriesText(graph) << "\n" <<
        SnapshotControlText(graph);
    for (const BB *block : *graph) {
        out << "B" << block->GetId() << " control=" << DescribeVertexRecord(block->GetControlVertex()) << "\n";
        out << "B" << block->GetId() << " phis[";
        for (const PhiVertex *phi : block->GetPhis()) {
            out << DescribeVertexRecord(phi) << ";";
        }
        out << "] v[";
        for (const NonControlVertex *vertex : block->GetVertices()) {
            out << DescribeVertexRecord(vertex) << ";";
        }
        out << "]\n";
    }
    return out.str();
}

std::string SnapshotMembershipText(Graph *graph)
{
    std::ostringstream out;
    out << SnapshotGraphScalarsText(graph) << "\n" << SnapshotRegistriesText(graph) << "\n" <<
        SnapshotControlText(graph);
    for (const BB *block : *graph) {
        out << "B" << block->GetId() << " phis[";
        for (const PhiVertex *phi : block->GetPhis()) {
            out << static_cast<const void *>(phi) << ";";
        }
        out << "] v[";
        for (const NonControlVertex *vertex : block->GetVertices()) {
            out << static_cast<const void *>(vertex) << ";";
        }
        out << "]\n";
    }
    return out.str();
}

void RemovalRequest::MarkRemoved(const Vertex *vertex)
{
    if (vertex != nullptr) {
        removed_.push_back(vertex);
    }
}

bool RemovalRequest::Contains(const Vertex *vertex) const
{
    return vertex != nullptr && std::find(removed_.begin(), removed_.end(), vertex) != removed_.end();
}

void MarkAllLiveExcept(Graph *graph, DceLiveSet &live, const RemovalRequest &request)
{
    for (BB *block : *graph) {
        for (PhiVertex *phi : block->GetPhis()) {
            if (!request.Contains(phi)) {
                live.MarkLive(phi);
            }
        }
        for (NonControlVertex *vertex : block->GetVertices()) {
            if (!request.Contains(vertex)) {
                live.MarkLive(vertex);
            }
        }
        if (!request.Contains(block->GetControlVertex())) {
            live.MarkLive(block->GetControlVertex());
        }
    }
    for (size_t index = 0, count = graph->GetParameterCount(); index < count; ++index) {
        live.MarkLive(graph->GetParameter(static_cast<uint32_t>(index)));
    }
    for (const auto &entry : graph->GetInt32Constants()) {
        live.MarkLive(entry.second);
    }
    for (const auto &entry : graph->GetInt64Constants()) {
        live.MarkLive(entry.second);
    }
    for (const auto &entry : graph->GetFloat64Constants()) {
        live.MarkLive(entry.second);
    }
    for (const auto &entry : graph->GetTaggedConstants()) {
        live.MarkLive(entry.second);
    }
    for (const auto &entry : graph->GetHeapConstants()) {
        live.MarkLive(entry.second);
    }
}

CompilerLogCapture::CompilerLogCapture()
{
    common::Log::Initialize({Level::WARN, static_cast<ComponentMark>(Component::COMPILER)});
}

void CompilerLogCapture::Begin()
{
    buffer_.str("");
    buffer_.clear();
    previous_ = std::cout.rdbuf(buffer_.rdbuf());
}

std::string CompilerLogCapture::End()
{
    std::cout.rdbuf(previous_);
    previous_ = nullptr;
    return buffer_.str();
}

DceResult FilterRemovals(Graph *graph, Chunk *chunk, const RemovalRequest &request)
{
    DceLiveSet live(chunk);
    MarkAllLiveExcept(graph, live, request);
    return DceGraphEditor::Filter(graph, live);
}

FilterOutcome FilterRemovalsCaptured(Graph *graph, Chunk *chunk, const RemovalRequest &request,
                                     CompilerLogCapture &capture)
{
    DceLiveSet live(chunk);
    MarkAllLiveExcept(graph, live, request);
    capture.Begin();
    FilterOutcome outcome;
    outcome.result = DceGraphEditor::Filter(graph, live);
    outcome.log = capture.End();
    return outcome;
}

DceRemovalClass ArkSteedDceTestBase::ClassifyProbe(Vertex *vertex)
{
    return DceRemovalPolicy::Classify(vertex);
}

ValueVertex *ArkSteedDceTestBase::RepresentationSample(ValueRepresentation representation)
{
    switch (representation) {
        case ValueRepresentation::INT32:
            return Vertex::New<Int32ConstantVertex>(&chunk_, 0, FIXTURE_INT32_VALUE);
        case ValueRepresentation::INT64:
            return Vertex::New<Int64ConstantVertex>(&chunk_, 0, 0);
        case ValueRepresentation::FLOAT64:
            return Vertex::New<Float64ConstantVertex>(&chunk_, 0, FIXTURE_FLOAT64_VALUE);
        default:
            return Vertex::New<TaggedConstantVertex>(&chunk_, 0,
                                                     JSTaggedValue::Undefined().GetRawData());
    }
}

void ArkSteedDceTestBase::InitializeInputs(Vertex *vertex, const std::vector<ValueVertex *> &inputs)
{
    uint32_t index = 0;
    for (ValueVertex *input : inputs) {
        vertex->SetInput(index++, input);
    }
}

void ArkSteedDceTestBase::SetControl(BB *block, ControlVertex *vertex)
{
    if (vertex != nullptr) {
        vertex->SetOwner(block);
    }
    block->SetControlVertex(vertex);
}

void ArkSteedDceTestBase::RunVerifier(Graph *graph)
{
    GraphProcessor<ArkSteedGraphVerifier> verifier(graph->GetChunk());
    verifier.Run(graph);
}

void ArkSteedDceTestBase::RunPreRegallocConsumers(Graph *graph)
{
    GraphProcessor<VertexInfoAllocateProcessor> infoAllocator;
    infoAllocator.Run(graph);
    GraphProcessor<ValueLocationConstraintProcessor> constraintProcessor;
    constraintProcessor.Run(graph);
    GraphProcessor<MaxCallDepthProcessor> callDepthProcessor;
    callDepthProcessor.Run(graph);
    GraphProcessor<LivenessProcessor> livenessProcessor;
    livenessProcessor.Run(graph);
}

void ArkSteedDceTestBase::InitializeAllocationValues(AllocationFixture &fixture)
{
    constexpr uint32_t objectSize = JSArray::SIZE + JSTaggedValue::TaggedTypeSize();
    auto *size = fixture.graph->GetInt64Constant(objectSize);
    auto *root = fixture.graph->GetHeapConstant(0, 0);
    fixture.taggedConstant = fixture.graph->GetTaggedConstant(JSTaggedValue::Undefined().GetRawData());
    fixture.check = NewVertex<CanAllocateInYoungVertex>({size});
    fixture.allocation = NewVertex<AllocateInYoungVertex>({size});
    auto *taggedSize = fixture.graph->GetTaggedConstant(JSTaggedValue(objectSize).GetRawData());
    fixture.slowAllocation = NewVertex<CallRuntimeVertex>({taggedSize}, RTSTUB_ID(AllocateInYoung));
    fixture.phi = NewVertex<PhiVertex>({fixture.allocation, fixture.slowAllocation}, VirtualRegister(0));
    fixture.initializer = NewVertex<InitializeEmptyArrayVertex>({fixture.phi, root, root}, objectSize);
    fixture.check->SetOwner(fixture.entry);
    fixture.entry->AddVertex(fixture.check);
    fixture.allocation->SetOwner(fixture.fastBlock);
    fixture.fastBlock->AddVertex(fixture.allocation);
    fixture.slowAllocation->SetOwner(fixture.slowBlock);
    fixture.slowBlock->AddVertex(fixture.slowAllocation);
    fixture.phi->SetOwner(fixture.merge);
    fixture.merge->AddPhiVertex(fixture.phi);
    fixture.initializer->SetOwner(fixture.merge);
    fixture.merge->AddVertex(fixture.initializer);
}

ArkSteedDceTestBase::AllocationFixture ArkSteedDceTestBase::BuildAllocationFixture()
{
    AllocationFixture fixture;
    fixture.graph = Graph::New(&chunk_);
    fixture.entry = BB::New(&chunk_);
    fixture.fastBlock = BB::New(&chunk_);
    fixture.slowBlock = BB::New(&chunk_);
    fixture.merge = BB::New(&chunk_);
    for (BB *block : {fixture.entry, fixture.fastBlock, fixture.slowBlock, fixture.merge}) {
        fixture.graph->Add(block);
    }
    InitializeAllocationValues(fixture);
    SetControl(fixture.entry,
               NewVertex<BranchIfTrueVertex>({fixture.check}, fixture.fastBlock, fixture.slowBlock));
    fixture.fastBlock->AddPredecessor(fixture.entry);
    fixture.slowBlock->AddPredecessor(fixture.entry);
    auto *fastJump = NewVertex<JumpVertex>({}, fixture.merge);
    fastJump->SetPredecessorId(fixture.merge->PredecessorCount());
    SetControl(fixture.fastBlock, fastJump);
    fixture.merge->AddPredecessor(fixture.fastBlock);
    auto *slowJump = NewVertex<JumpVertex>({}, fixture.merge);
    slowJump->SetPredecessorId(fixture.merge->PredecessorCount());
    SetControl(fixture.slowBlock, slowJump);
    fixture.merge->AddPredecessor(fixture.slowBlock);
    SetControl(fixture.merge, NewVertex<ReturnVertex>({fixture.taggedConstant}));
    return fixture;
}

bool ArkSteedDceTestBase::InitializeEditorConstants(EditorFixture &fixture)
{
    fixture.int32Constant = fixture.graph->GetInt32Constant(FIXTURE_INT32_VALUE);
    fixture.taggedConstant = fixture.graph->GetTaggedConstant(JSTaggedValue::Undefined().GetRawData());
    ValueVertex *int64Constant = fixture.graph->GetInt64Constant(0);
    ValueVertex *float64Constant = fixture.graph->GetFloat64Constant(FIXTURE_FLOAT64_VALUE);
    ValueVertex *heapConstant = fixture.graph->GetHeapConstant(0, 0);
    if (fixture.int32Constant == nullptr || fixture.taggedConstant == nullptr || int64Constant == nullptr ||
        float64Constant == nullptr || heapConstant == nullptr) {
        ADD_FAILURE() << "editor fixture construction failed: a constant accessor returned null";
        return false;
    }
    return true;
}

bool ArkSteedDceTestBase::InitializeEditorValues(EditorFixture &fixture)
{
    fixture.deadChainHead = NewVertex<I32AddVertex>({fixture.int32Constant, fixture.int32Constant});
    if (fixture.deadChainHead != nullptr) {
        fixture.deadChainHead->SetOwner(fixture.entry);
    }
    fixture.deadChainTail = NewVertex<I32ToTaggedIntVertex>({fixture.deadChainHead});
    if (fixture.deadChainTail != nullptr) {
        fixture.deadChainTail->SetOwner(fixture.entry);
    }
    fixture.liveProducer = NewVertex<I32AddVertex>({fixture.int32Constant, fixture.int32Constant});
    if (fixture.liveProducer != nullptr) {
        fixture.liveProducer->SetOwner(fixture.entry);
    }
    fixture.deadConsumer = NewVertex<I32AddVertex>({fixture.liveProducer, fixture.int32Constant});
    if (fixture.deadConsumer != nullptr) {
        fixture.deadConsumer->SetOwner(fixture.entry);
    }
    fixture.deadSink = NewVertex<I32ToTaggedIntVertex>({fixture.liveProducer});
    if (fixture.deadSink != nullptr) {
        fixture.deadSink->SetOwner(fixture.entry);
    }
    fixture.parameter = NewVertex<InitialValueVertex>({}, CALL_TARGET_FP_SLOT_INDEX);
    if (fixture.parameter != nullptr) {
        fixture.parameter->SetOwner(fixture.entry);
        fixture.graph->AddParameter(fixture.parameter);
    }
    fixture.branch = NewVertex<BranchIfInt32CompareVertex>(
        {fixture.int32Constant, fixture.int32Constant}, fixture.ifTrue, fixture.ifFalse, Condition::EQUAL);
    if (fixture.branch == nullptr || fixture.deadChainHead == nullptr || fixture.deadChainTail == nullptr ||
        fixture.liveProducer == nullptr || fixture.deadConsumer == nullptr || fixture.deadSink == nullptr ||
        fixture.parameter == nullptr) {
        ADD_FAILURE() << "editor fixture construction failed: Vertex::New returned a null vertex";
        return false;
    }

    fixture.entry->AddVertex(fixture.deadChainHead);
    fixture.entry->AddVertex(fixture.deadChainTail);
    fixture.entry->AddVertex(fixture.liveProducer);
    fixture.entry->AddVertex(fixture.deadConsumer);
    fixture.entry->AddVertex(fixture.deadSink);
    fixture.entry->AddVertex(fixture.parameter);
    fixture.branch->SetOwner(fixture.entry);
    SetControl(fixture.entry, fixture.branch);
    return true;
}

bool ArkSteedDceTestBase::InitializeEditorMerge(EditorFixture &fixture)
{
    fixture.trueJump = NewVertex<JumpVertex>({}, fixture.merge);
    fixture.falseJump = NewVertex<JumpVertex>({}, fixture.merge);
    if (fixture.trueJump == nullptr || fixture.falseJump == nullptr) {
        ADD_FAILURE() << "editor fixture construction failed: JumpVertex::New returned null";
        return false;
    }
    fixture.trueJump->SetOwner(fixture.ifTrue);
    SetControl(fixture.ifTrue, fixture.trueJump);
    fixture.falseJump->SetOwner(fixture.ifFalse);
    SetControl(fixture.ifFalse, fixture.falseJump);

    fixture.livePhi = NewVertex<PhiVertex>({fixture.liveProducer, fixture.liveProducer}, VirtualRegister(0));
    fixture.deadPhi = NewVertex<PhiVertex>({fixture.int32Constant, fixture.int32Constant}, VirtualRegister(1));
    if (fixture.livePhi == nullptr || fixture.deadPhi == nullptr) {
        ADD_FAILURE() << "editor fixture construction failed: PhiVertex::New returned null";
        return false;
    }
    fixture.livePhi->SetOwner(fixture.merge);
    fixture.deadPhi->SetOwner(fixture.merge);
    fixture.merge->AddPhiVertex(fixture.livePhi);
    fixture.merge->AddPhiVertex(fixture.deadPhi);

    fixture.mergeReturn = NewVertex<ReturnVertex>({fixture.livePhi});
    if (fixture.mergeReturn == nullptr) {
        ADD_FAILURE() << "editor fixture construction failed: ReturnVertex::New returned null";
        return false;
    }
    fixture.mergeReturn->SetOwner(fixture.merge);
    SetControl(fixture.merge, fixture.mergeReturn);
    return true;
}

ArkSteedDceTestBase::EditorFixture ArkSteedDceTestBase::BuildEditorFixture()
{
    EditorFixture fixture;
    fixture.graph = Graph::New(&chunk_);
    fixture.entry = BB::New(&chunk_);
    fixture.ifTrue = BB::New(&chunk_);
    fixture.ifFalse = BB::New(&chunk_);
    fixture.merge = BB::New(&chunk_);
    if (fixture.graph == nullptr || fixture.entry == nullptr || fixture.ifTrue == nullptr ||
        fixture.ifFalse == nullptr || fixture.merge == nullptr) {
        ADD_FAILURE() << "editor fixture construction failed: Graph::New or BB::New returned null";
        return EditorFixture {};
    }
    fixture.graph->Add(fixture.entry);
    fixture.graph->Add(fixture.ifTrue);
    fixture.graph->Add(fixture.ifFalse);
    fixture.graph->Add(fixture.merge);

    if (!InitializeEditorConstants(fixture)) {
        return EditorFixture {};
    }

    if (!InitializeEditorValues(fixture)) {
        return EditorFixture {};
    }

    // Edge splitting: the conditional targets carry no Phis and finish with an
    // unconditional jump to the merge block. `FinishBlockWithJump` stores the
    // target's current predecessor count and then appends the source block, so
    // the fixture sets the same id before adding the same predecessor.
    if (!InitializeEditorMerge(fixture)) {
        return EditorFixture {};
    }

    // Predecessor order follows the production construction order: the branch
    // is the single predecessor of both edge-split targets, and each edge-split
    // target is an ordinary predecessor of the merge block at the index its
    // unconditional control vertex stores.
    fixture.ifTrue->AddPredecessor(fixture.entry);
    fixture.ifFalse->AddPredecessor(fixture.entry);
    fixture.trueJump->SetPredecessorId(fixture.merge->PredecessorCount());
    fixture.merge->AddPredecessor(fixture.ifTrue);
    fixture.falseJump->SetPredecessorId(fixture.merge->PredecessorCount());
    fixture.merge->AddPredecessor(fixture.ifFalse);
    return fixture;
}

void ArkSteedDceTestBase::MarkDeadEditorSelection(const EditorFixture &fixture, RemovalRequest &request)
{
    request.MarkRemoved(fixture.deadChainHead);
    request.MarkRemoved(fixture.deadChainTail);
    request.MarkRemoved(fixture.deadConsumer);
    request.MarkRemoved(fixture.deadSink);
    request.MarkRemoved(fixture.deadPhi);
}

bool ArkSteedDceTestBase::InitializeLoopControls(LoopFixture &fixture)
{
    fixture.headerPhi = NewVertex<PhiVertex>({fixture.entryValue, fixture.backedgeValue}, VirtualRegister(0));
    fixture.headerBranch = NewVertex<BranchIfTrueVertex>({fixture.headerPhi}, fixture.body, fixture.exit);
    fixture.preheaderJump = Vertex::New<JumpVertex>(&chunk_, 0, fixture.header);
    fixture.backedge = Vertex::New<JumpLoopVertex>(&chunk_, 0, &chunk_, fixture.header);
    fixture.exitReturn = NewVertex<ReturnVertex>({fixture.entryValue});
    if (fixture.headerPhi == nullptr || fixture.headerBranch == nullptr || fixture.preheaderJump == nullptr ||
        fixture.backedge == nullptr || fixture.exitReturn == nullptr) {
        ADD_FAILURE() << "loop fixture construction failed: Vertex::New returned null";
        return false;
    }
    fixture.headerPhi->SetOwner(fixture.header);
    fixture.header->AddPhiVertex(fixture.headerPhi);
    fixture.header->SetIsLoopHeader(true);
    fixture.headerBranch->SetOwner(fixture.header);
    SetControl(fixture.header, fixture.headerBranch);
    fixture.preheaderJump->SetOwner(fixture.preheader);
    SetControl(fixture.preheader, fixture.preheaderJump);
    fixture.backedge->SetOwner(fixture.body);
    SetControl(fixture.body, fixture.backedge);
    fixture.exitReturn->SetOwner(fixture.exit);
    SetControl(fixture.exit, fixture.exitReturn);
    return true;
}

ArkSteedDceTestBase::LoopFixture ArkSteedDceTestBase::BuildLoopFixture()
{
    LoopFixture fixture;
    fixture.graph = Graph::New(&chunk_);
    fixture.preheader = BB::New(&chunk_);
    fixture.header = BB::New(&chunk_);
    fixture.body = BB::New(&chunk_);
    fixture.exit = BB::New(&chunk_);
    if (fixture.graph == nullptr || fixture.preheader == nullptr || fixture.header == nullptr ||
        fixture.body == nullptr || fixture.exit == nullptr) {
        ADD_FAILURE() << "loop fixture construction failed: Graph::New or BB::New returned null";
        return LoopFixture {};
    }
    fixture.graph->Add(fixture.preheader);
    fixture.graph->Add(fixture.header);
    fixture.graph->Add(fixture.body);
    fixture.graph->Add(fixture.exit);

    fixture.entryValue = fixture.graph->GetInt32Constant(LOOP_ENTRY_VALUE);
    fixture.backedgeValue = NewVertex<I32AddVertex>({fixture.entryValue, fixture.entryValue});
    if (fixture.entryValue == nullptr || fixture.backedgeValue == nullptr) {
        ADD_FAILURE() << "loop fixture construction failed: a constant or value returned null";
        return LoopFixture {};
    }
    fixture.backedgeValue->SetOwner(fixture.body);
    fixture.body->AddVertex(fixture.backedgeValue);

    if (!InitializeLoopControls(fixture)) {
        return LoopFixture {};
    }

    // Predecessor order follows the production construction order: the
    // preheader enters the header at id 0, the branch adds both targets, and
    // the back edge is appended last at id 1.
    fixture.preheaderJump->SetPredecessorId(fixture.header->PredecessorCount());
    fixture.header->AddPredecessor(fixture.preheader);
    fixture.body->AddPredecessor(fixture.header);
    fixture.exit->AddPredecessor(fixture.header);
    fixture.backedge->SetPredecessorId(fixture.header->PredecessorCount());
    fixture.header->AddPredecessor(fixture.body);
    return fixture;
}

bool ArkSteedDceTestBase::InitializeCatchValues(CatchFixture &fixture)
{
    fixture.firstThrowable = NewVertex<CallRuntimeVertex>({fixture.first}, RTSTUB_ID(NewTaggedArray),
                                                          SideEffectKind::SAFE_CALL);
    fixture.secondThrowable = NewVertex<CallRuntimeVertex>({fixture.second}, RTSTUB_ID(NewTaggedArray),
                                                           SideEffectKind::SAFE_CALL);
    fixture.entryReturn = NewVertex<ReturnVertex>({fixture.first});
    fixture.catchJump = NewVertex<JumpVertex>({}, fixture.handler);
    fixture.handlerReturn = NewVertex<ReturnVertex>({fixture.second});
    if (fixture.firstThrowable == nullptr || fixture.secondThrowable == nullptr ||
        fixture.entryReturn == nullptr || fixture.catchJump == nullptr || fixture.handlerReturn == nullptr) {
        ADD_FAILURE() << "catch fixture construction failed: Vertex::New returned null";
        return false;
    }
    fixture.firstThrowable->SetOwner(fixture.entry);
    fixture.secondThrowable->SetOwner(fixture.entry);
    fixture.entryReturn->SetOwner(fixture.entry);
    fixture.catchJump->SetOwner(fixture.catchBlock);
    fixture.handlerReturn->SetOwner(fixture.handler);
    return true;
}

ArkSteedDceTestBase::CatchFixture ArkSteedDceTestBase::BuildCatchFixture()
{
    CatchFixture fixture;
    fixture.graph = Graph::New(&chunk_);
    fixture.entry = BB::New(&chunk_);
    fixture.catchBlock = BB::New(&chunk_);
    fixture.handler = BB::New(&chunk_);
    if (fixture.graph == nullptr || fixture.entry == nullptr || fixture.catchBlock == nullptr ||
        fixture.handler == nullptr) {
        ADD_FAILURE() << "catch fixture construction failed: Graph::New or BB::New returned null";
        return CatchFixture {};
    }
    fixture.graph->Add(fixture.entry);
    fixture.graph->Add(fixture.catchBlock);
    fixture.graph->Add(fixture.handler);
    fixture.first = fixture.graph->GetTaggedConstant(JSTaggedValue::Undefined().GetRawData());
    fixture.second = fixture.graph->GetTaggedConstant(JSTaggedValue::Null().GetRawData());
    if (fixture.first == nullptr || fixture.second == nullptr) {
        ADD_FAILURE() << "catch fixture construction failed: a constant accessor returned null";
        return CatchFixture {};
    }
    if (!InitializeCatchValues(fixture)) {
        return CatchFixture {};
    }

    // Production catch-header structure: the handler is reached only through
    // the exceptional edge, so it has zero ordinary predecessors, and the
    // handler flag makes `ArkSteedRegisterAllocator::InitializeBlockState`
    // clear register values at handler entry.
    fixture.catchBlock->SetIsExceptionHandler(true);
    SetControl(fixture.catchBlock, fixture.catchJump);
    SetControl(fixture.handler, fixture.handlerReturn);
    fixture.catchJump->SetPredecessorId(fixture.handler->PredecessorCount());
    fixture.handler->AddPredecessor(fixture.catchBlock);

    fixture.entry->AddVertex(fixture.firstThrowable);
    fixture.entry->AddVertex(fixture.secondThrowable);
    SetControl(fixture.entry, fixture.entryReturn);
    fixture.firstThrowable->LoadCatchBlock(fixture.catchBlock, 0);
    fixture.secondThrowable->LoadCatchBlock(fixture.catchBlock, 1);
    return fixture;
}

PhiVertex *ArkSteedDceTestBase::AddCatchPhi(const CatchFixture &fixture, const std::vector<ValueVertex *> &inputs)
{
    PhiVertex *phi = NewVertex<PhiVertex>(inputs, VirtualRegister(0));
    if (phi == nullptr) {
        ADD_FAILURE() << "catch fixture construction failed: PhiVertex::New returned null";
        return nullptr;
    }
    phi->SetOwner(fixture.catchBlock);
    fixture.catchBlock->AddPhiVertex(phi);
    return phi;
}

bool ArkSteedDceTestBase::InitializeHiddenValues(HiddenOnlyFixture &fixture, ValueVertex *int32Constant)
{
    fixture.deadPlain = NewVertex<I32AddVertex>({int32Constant, int32Constant});
    fixture.eagerProducer = NewVertex<I32AddVertex>({int32Constant, int32Constant});
    fixture.lazyProducer = NewVertex<I32AddVertex>({int32Constant, int32Constant});
    fixture.catchProducer = NewVertex<I32AddVertex>({int32Constant, int32Constant});
    fixture.eagerConsumer = NewVertex<I32AddWithOverflowVertex>({int32Constant, int32Constant}, &chunk_, 0);
    fixture.lazyConsumer = NewVertex<CallRuntimeVertex>(std::vector<ValueVertex *> {},
                                                        RTSTUB_ID(NewTaggedArray), SideEffectKind::SAFE_CALL);
    fixture.catchThrowable = NewVertex<CallRuntimeVertex>({fixture.taggedConstant}, RTSTUB_ID(NewTaggedArray),
                                                          SideEffectKind::SAFE_CALL);
    fixture.entryReturn = NewVertex<ReturnVertex>({fixture.taggedConstant});
    fixture.catchJump = NewVertex<JumpVertex>({}, fixture.handler);
    fixture.catchPhi = NewVertex<PhiVertex>({fixture.catchProducer}, VirtualRegister(0));
    fixture.handlerReturn = NewVertex<ReturnVertex>({fixture.catchPhi});
    if (fixture.deadPlain == nullptr || fixture.eagerProducer == nullptr || fixture.lazyProducer == nullptr ||
        fixture.catchProducer == nullptr || fixture.eagerConsumer == nullptr || fixture.lazyConsumer == nullptr ||
        fixture.catchThrowable == nullptr || fixture.entryReturn == nullptr || fixture.catchJump == nullptr ||
        fixture.catchPhi == nullptr || fixture.handlerReturn == nullptr) {
        ADD_FAILURE() << "hidden-only fixture construction failed: Vertex::New returned null";
        return false;
    }
    for (NonControlVertex *vertex :
         std::vector<NonControlVertex *> {fixture.deadPlain, fixture.eagerProducer, fixture.lazyProducer,
                                          fixture.catchProducer, fixture.eagerConsumer, fixture.lazyConsumer,
                                          fixture.catchThrowable}) {
        vertex->SetOwner(fixture.entry);
        fixture.entry->AddVertex(vertex);
    }
    SetControl(fixture.entry, fixture.entryReturn);
    return true;
}

bool ArkSteedDceTestBase::InitializeHiddenState(HiddenOnlyFixture &fixture)
{
    I32AddWithOverflowVertex::EagerDeoptFrameState eagerFrameState(&chunk_);
    eagerFrameState.emplace_back(VRegIDType(0), fixture.eagerProducer, DeoptTranslationKind::RAW_INT32);
    fixture.eagerConsumer->SetEagerDeoptFrameState(std::move(eagerFrameState));
    auto *lazyFrameState = chunk_.New<CallRuntimeVertex::LazyDeoptFrameState>(&chunk_);
    if (lazyFrameState == nullptr) {
        ADD_FAILURE() << "hidden-only fixture construction failed: lazy frame state allocation failed";
        return false;
    }
    lazyFrameState->emplace_back(VRegIDType(0), fixture.lazyProducer, DeoptTranslationKind::RAW_INT32);
    fixture.lazyConsumer->SetLazyDeoptFrameState(lazyFrameState, 0);

    fixture.catchBlock->SetIsExceptionHandler(true);
    fixture.catchPhi->SetOwner(fixture.catchBlock);
    fixture.catchBlock->AddPhiVertex(fixture.catchPhi);
    fixture.catchJump->SetOwner(fixture.catchBlock);
    SetControl(fixture.catchBlock, fixture.catchJump);
    fixture.catchJump->SetPredecessorId(fixture.handler->PredecessorCount());
    fixture.handler->AddPredecessor(fixture.catchBlock);
    fixture.handlerReturn->SetOwner(fixture.handler);
    SetControl(fixture.handler, fixture.handlerReturn);
    fixture.catchThrowable->LoadCatchBlock(fixture.catchBlock, 0);
    return true;
}

ArkSteedDceTestBase::HiddenOnlyFixture ArkSteedDceTestBase::BuildHiddenOnlyFixture()
{
    HiddenOnlyFixture fixture;
    fixture.graph = Graph::New(&chunk_);
    fixture.entry = BB::New(&chunk_);
    fixture.catchBlock = BB::New(&chunk_);
    fixture.handler = BB::New(&chunk_);
    if (fixture.graph == nullptr || fixture.entry == nullptr || fixture.catchBlock == nullptr ||
        fixture.handler == nullptr) {
        ADD_FAILURE() << "hidden-only fixture construction failed: Graph::New or BB::New returned null";
        return HiddenOnlyFixture {};
    }
    fixture.graph->Add(fixture.entry);
    fixture.graph->Add(fixture.catchBlock);
    fixture.graph->Add(fixture.handler);

    ValueVertex *int32Constant = fixture.graph->GetInt32Constant(FIXTURE_INT32_VALUE);
    fixture.taggedConstant = fixture.graph->GetTaggedConstant(JSTaggedValue::Undefined().GetRawData());
    if (int32Constant == nullptr || fixture.taggedConstant == nullptr) {
        ADD_FAILURE() << "hidden-only fixture construction failed: a constant accessor returned null";
        return HiddenOnlyFixture {};
    }

    if (!InitializeHiddenValues(fixture, int32Constant)) {
        return HiddenOnlyFixture {};
    }

    if (!InitializeHiddenState(fixture)) {
        return HiddenOnlyFixture {};
    }
    return fixture;
}

bool ArkSteedDceTestBase::InitializeCatchClosureValues(CatchClosureFixture &fixture, ValueVertex *int32Constant)
{
    fixture.intProducer = NewVertex<I32AddVertex>({int32Constant, int32Constant});
    fixture.shared = NewVertex<I32ToTaggedIntVertex>({fixture.intProducer});
    fixture.liveOnly = NewVertex<ConvertHoleToUndefinedVertex>({fixture.second});
    fixture.deadHeadProducer = NewVertex<ConvertHoleToUndefinedVertex>({fixture.shared});
    fixture.firstThrowable = NewVertex<CallRuntimeVertex>({fixture.first}, RTSTUB_ID(NewTaggedArray),
                                                          SideEffectKind::SAFE_CALL);
    fixture.secondThrowable = NewVertex<CallRuntimeVertex>({fixture.second}, RTSTUB_ID(NewTaggedArray),
                                                           SideEffectKind::SAFE_CALL);
    fixture.entryReturn = NewVertex<ReturnVertex>({fixture.first});
    fixture.catchJump = NewVertex<JumpVertex>({}, fixture.handler);
    fixture.handlerReturn = NewVertex<ReturnVertex>({fixture.second});
    fixture.livePhi = NewVertex<PhiVertex>({fixture.liveOnly, fixture.shared}, VirtualRegister(0));
    fixture.deadHead = NewVertex<PhiVertex>({fixture.deadHeadProducer, fixture.second}, VirtualRegister(1));
    fixture.deadTail = NewVertex<PhiVertex>({fixture.deadHead, fixture.second}, VirtualRegister(DEAD_TAIL_VREG));
    if (fixture.intProducer == nullptr || fixture.shared == nullptr || fixture.liveOnly == nullptr ||
        fixture.deadHeadProducer == nullptr || fixture.firstThrowable == nullptr ||
        fixture.secondThrowable == nullptr || fixture.entryReturn == nullptr || fixture.catchJump == nullptr ||
        fixture.handlerReturn == nullptr || fixture.livePhi == nullptr || fixture.deadHead == nullptr ||
        fixture.deadTail == nullptr) {
        ADD_FAILURE() << "catch-closure fixture construction failed: Vertex::New returned null";
        return false;
    }
    return true;
}

void ArkSteedDceTestBase::InstallCatchClosure(CatchClosureFixture &fixture, bool reverseOrder)
{
    // Entry membership: the producers precede the throwables that reference
    // their values at their exceptional slots.
    const std::vector<NonControlVertex *> producers {fixture.intProducer, fixture.shared, fixture.liveOnly,
                                                     fixture.deadHeadProducer};
    for (NonControlVertex *vertex : producers) {
        vertex->SetOwner(fixture.entry);
        fixture.entry->AddVertex(vertex);
    }
    if (reverseOrder) {
        fixture.entry->AddVertex(fixture.secondThrowable);
        fixture.entry->AddVertex(fixture.firstThrowable);
    } else {
        fixture.entry->AddVertex(fixture.firstThrowable);
        fixture.entry->AddVertex(fixture.secondThrowable);
    }
    fixture.firstThrowable->SetOwner(fixture.entry);
    fixture.secondThrowable->SetOwner(fixture.entry);
    fixture.entryReturn->SetOwner(fixture.entry);
    SetControl(fixture.entry, fixture.entryReturn);

    fixture.catchBlock->SetIsExceptionHandler(true);
    for (PhiVertex *phi : std::vector<PhiVertex *> {fixture.livePhi, fixture.deadHead, fixture.deadTail}) {
        phi->SetOwner(fixture.catchBlock);
    }
    if (reverseOrder) {
        fixture.catchBlock->AddPhiVertex(fixture.deadTail);
        fixture.catchBlock->AddPhiVertex(fixture.deadHead);
        fixture.catchBlock->AddPhiVertex(fixture.livePhi);
    } else {
        fixture.catchBlock->AddPhiVertex(fixture.livePhi);
        fixture.catchBlock->AddPhiVertex(fixture.deadHead);
        fixture.catchBlock->AddPhiVertex(fixture.deadTail);
    }
    // The handler consumes the live catch Phi, which is what roots it and
    // every one of its own inputs.
    fixture.handlerReturn->SetInput(0, fixture.livePhi);
    fixture.handlerReturn->SetOwner(fixture.handler);
    fixture.catchJump->SetOwner(fixture.catchBlock);
    SetControl(fixture.catchBlock, fixture.catchJump);
    SetControl(fixture.handler, fixture.handlerReturn);
    fixture.catchJump->SetPredecessorId(fixture.handler->PredecessorCount());
    fixture.handler->AddPredecessor(fixture.catchBlock);
    fixture.firstThrowable->LoadCatchBlock(fixture.catchBlock, 0);
    fixture.secondThrowable->LoadCatchBlock(fixture.catchBlock, 1);
}

ArkSteedDceTestBase::CatchClosureFixture ArkSteedDceTestBase::BuildCatchClosureFixture(bool reverseOrder)
{
    CatchClosureFixture fixture;
    fixture.graph = Graph::New(&chunk_);
    fixture.entry = BB::New(&chunk_);
    fixture.catchBlock = BB::New(&chunk_);
    fixture.handler = BB::New(&chunk_);
    if (fixture.graph == nullptr || fixture.entry == nullptr || fixture.catchBlock == nullptr ||
        fixture.handler == nullptr) {
        ADD_FAILURE() << "catch-closure fixture construction failed: Graph::New or BB::New returned null";
        return CatchClosureFixture {};
    }
    fixture.graph->Add(fixture.entry);
    fixture.graph->Add(fixture.catchBlock);
    fixture.graph->Add(fixture.handler);

    fixture.first = fixture.graph->GetTaggedConstant(JSTaggedValue::Undefined().GetRawData());
    fixture.second = fixture.graph->GetTaggedConstant(JSTaggedValue::Null().GetRawData());
    ValueVertex *int32Constant = fixture.graph->GetInt32Constant(FIXTURE_INT32_VALUE);
    if (fixture.first == nullptr || fixture.second == nullptr || int32Constant == nullptr) {
        ADD_FAILURE() << "catch-closure fixture construction failed: a constant accessor returned null";
        return CatchClosureFixture {};
    }

    if (!InitializeCatchClosureValues(fixture, int32Constant)) {
        return CatchClosureFixture {};
    }

    InstallCatchClosure(fixture, reverseOrder);
    return fixture;
}

void ArkSteedDceTest::ExpectRemovalClass(Vertex *vertex, DceRemovalClass expected)
{
    ASSERT_NE(vertex, nullptr) << "fixture construction failed";
    EXPECT_EQ(static_cast<uint32_t>(ClassifyProbe(vertex)), static_cast<uint32_t>(expected));
}

void ArkSteedDceTest::ExpectRemovableValues(const std::vector<ValueVertex *> &values)
{
    for (ValueVertex *value : values) {
        ASSERT_NE(value, nullptr) << "removable fixture construction failed";
        EXPECT_TRUE(value->CanBeRemovedIfDead()) << OpcodeToString(value->GetOpcode());
        ExpectRemovalClass(value, DceRemovalClass::RemovableValue);
    }
}

void ArkSteedDceTest::ExpectRequiredControls(const std::vector<Vertex *> &values)
{
    for (Vertex *value : values) {
        ASSERT_NE(value, nullptr) << "required fixture construction failed";
        EXPECT_FALSE(value->CanBeRemovedIfDead()) << OpcodeToString(value->GetOpcode());
        ExpectRemovalClass(value, DceRemovalClass::Required);
    }
}

void ArkSteedDceTest::CheckRemovableArithmetic()
{
    ExpectRemovableValues({NewRemovableVertex<I32AddVertex>(), NewRemovableVertex<I32SubVertex>(),
        NewRemovableVertex<I32MulVertex>(), NewRemovableVertex<F64AddVertex>(),
        NewRemovableVertex<F64SubVertex>(), NewRemovableVertex<F64MulVertex>(),
        NewRemovableVertex<F64DivVertex>(), NewRemovableVertex<F64NegVertex>()});
}

void ArkSteedDceTest::CheckRemovableBitwiseAndComparisons()
{
    ExpectRemovableValues({NewRemovableVertex<I32BitwiseBinaryVertex>(IntBitwiseKind::BITWISE_AND),
        NewRemovableVertex<I32BNotVertex>(),
        NewRemovableVertex<I64BitwiseBinaryVertex>(IntBitwiseKind::BITWISE_XOR),
        NewRemovableVertex<I32ConditionCheckVertex>(Condition::EQUAL),
        NewRemovableVertex<F64ConditionCheckVertex>(Condition::LESS_THAN),
        NewRemovableVertex<Float64ToBooleanVertex>(true), NewRemovableVertex<TaggedEqualVertex>(),
        NewRemovableVertex<TaggedNotEqualVertex>()});
}

void ArkSteedDceTest::CheckRemovableConversionsAndMerge()
{
    ExpectRemovableValues({NewRemovableVertex<ConvertHoleToUndefinedVertex>(),
        NewRemovableVertex<TaggedIntToI32Vertex>(),
        NewVertex<I32ToTaggedIntVertex>({RepresentationSample(ValueRepresentation::INT32)}),
        NewRemovableVertex<U32ToTaggedNumberVertex>(),
        NewVertex<RawI64ToTaggedVertex>({RepresentationSample(ValueRepresentation::INT64)}),
        NewVertex<TaggedToRawI64Vertex>({RepresentationSample(ValueRepresentation::TAGGED)}),
        NewRemovableVertex<I32ToF64Vertex>(), NewRemovableVertex<F64ToI32TruncVertex>(),
        NewRemovableVertex<I32ToUint8ClampedVertex>(), NewRemovableVertex<F64ToUint8ClampedVertex>(),
        NewRemovableVertex<F64ToTaggedDoubleVertex>(),
        NewVertex<PhiVertex>({RepresentationSample(ValueRepresentation::TAGGED),
                             RepresentationSample(ValueRepresentation::TAGGED)}, VirtualRegister(FIRST_VREG))});
}

void ArkSteedDceTest::CheckSideEffectsAndCseDoNotAuthorizeRemoval()
{
    ValueVertex *input = RepresentationSample(ValueRepresentation::INT32);
    auto *divide = NewVertex<I32DivVertex>({input, input});
    auto *add = NewVertex<I32AddVertex>({input, input});
    ASSERT_NE(divide, nullptr);
    ASSERT_NE(add, nullptr);
    EXPECT_FALSE(divide->MayHaveSideEffects());
    EXPECT_FALSE(add->MayHaveSideEffects());
    EXPECT_TRUE(divide->CanParticipateInCSE());
    EXPECT_TRUE(add->CanParticipateInCSE());
    EXPECT_FALSE(divide->CanBeRemovedIfDead());
    EXPECT_TRUE(add->CanBeRemovedIfDead());
    ExpectRemovalClass(divide, DceRemovalClass::Required);
    ExpectRemovalClass(add, DceRemovalClass::RemovableValue);
}

void ArkSteedDceTest::CheckRegistryAndStructuralValuesRemainRequired()
{
    ExpectRequiredControls({
        Vertex::New<Int32ConstantVertex>(&chunk_, SAMPLE_VERTEX_ID, SAMPLE_INT32_VALUE),
        Vertex::New<InitialValueVertex>(&chunk_, SAMPLE_VERTEX_ID, SAMPLE_BINDING_INDEX),
        Vertex::New<ActualArgcVertex>(&chunk_, SAMPLE_VERTEX_ID),
        Vertex::New<GapMoveVertex>(&chunk_, SAMPLE_VERTEX_ID, AllocatedState(), AllocatedState())});
    EXPECT_EQ(static_cast<uint32_t>(ClassifyProbe(nullptr)), static_cast<uint32_t>(DceRemovalClass::Required));
}

void ArkSteedDceTest::CheckUnknownOpcodesIgnoreRemovalAuthorization()
{
    auto *unknown = Vertex::New<Int32ConstantVertex>(&chunk_, SAMPLE_VERTEX_ID, SAMPLE_INT32_VALUE);
    ASSERT_NE(unknown, nullptr);
    SetPropertyFlagsForTest(unknown, VertexPropertyFlag::CAN_BE_REMOVED_IF_DEAD);
    EXPECT_TRUE(unknown->CanBeRemovedIfDead());
    SetOpcodeForTest(unknown, VertexOpcode::INVALID);
    ExpectRemovalClass(unknown, DceRemovalClass::Required);
    SetOpcodeForTest(unknown, UNKNOWN_OPCODE);
    ExpectRemovalClass(unknown, DceRemovalClass::Required);
}

void ArkSteedDceTest::CheckCallsRemainRequired(ValueVertex *address, ValueVertex *tagged)
{
    const std::vector<ValueVertex *> callInputs {tagged, tagged, tagged, tagged};
    ExpectRequiredControls({
        NewVertex<CallRuntimeVertex>({}, RTSTUB_ID(NewTaggedArray), SideEffectKind::SAFE_CALL),
        NewVertex<CallCommonStubVertex>({address}, CommonStubID::ToBooleanTrue, SideEffectKind::SAFE_CALL),
        NewVertex<CallVertex>(callInputs, SAMPLE_ARGUMENT_COUNT),
        NewVertex<CallKnownJSFunctionVertex>({tagged, tagged, tagged, tagged}, SAMPLE_ARGUMENT_COUNT,
                                             SAMPLE_ARGUMENT_COUNT, SAMPLE_ARGUMENT_COUNT),
        NewVertex<CallSelfVertex>({tagged, tagged, tagged, tagged, tagged},
                                  SAMPLE_ARGUMENT_COUNT, SAMPLE_ARGUMENT_COUNT),
        NewVertex<CallKnownBuiltinVertex>(callInputs, SAMPLE_ARGUMENT_COUNT,
            kungfu::BuiltinsStubCSigns::ID::BooleanConstructor, BuiltinCallEntryKind::NATIVE_CPP)});
}

void ArkSteedDceTest::CheckWritesAndAllocationsRemainRequired(ValueVertex *address, ValueVertex *tagged)
{
    const std::vector<ValueVertex *> taggedInputs {tagged, tagged};
    const std::vector<ValueVertex *> storeInputs {address, tagged, tagged};
    const std::vector<ValueVertex *> barrierInputs {address, tagged};
    ExpectRequiredControls({
        NewVertex<StoreTaggedFieldVertex>(taggedInputs, SAMPLE_FIELD_OFFSET),
        NewVertex<StoreTaggedElementVertex>(storeInputs),
        NewVertex<SetValueWithBarrierVertex>(barrierInputs, SAMPLE_ADDRESS_OFFSET),
        NewVertex<TransitionHClassWithBarrierVertex>(barrierInputs),
        NewVertex<EnsureWritableElementsVertex>(taggedInputs),
        NewVertex<AllocateInYoungVertex>({address}),
        NewVertex<InitializeEmptyArrayVertex>({tagged, tagged, tagged},
                                              JSArray::SIZE + JSTaggedValue::TaggedTypeSize())});
}

void ArkSteedDceTest::CheckExceptionalAndMemoryOperationsRemainRequired(ValueVertex *address, ValueVertex *tagged)
{
    ExpectRequiredControls({
        NewVertex<DeoptVertex>({}, &chunk_, kungfu::DeoptType::INSUFFICIENTPROFILE, SAMPLE_BYTECODE_OFFSET),
        NewVertex<ThrowVertex>({tagged}, RTSTUB_ID(Throw)),
        NewVertex<LoadTaggedFromAddressVertex>({address}, SAMPLE_ADDRESS_OFFSET),
        NewVertex<LoadI32FromAddressVertex>({address}, SAMPLE_ADDRESS_OFFSET),
        NewVertex<LoadI64FromAddressVertex>({address}, SAMPLE_ADDRESS_OFFSET),
        NewVertex<LoadF64FromAddressVertex>({address}, SAMPLE_ADDRESS_OFFSET),
        NewVertex<StringEqualVertex>({tagged, tagged, tagged}),
        NewVertex<DoubleToInt32CallVertex>({address}), NewVertex<I32DivVertex>({address, address})});
}

ValueVertex *ArkSteedDceTest::NewTaggedSample(JSTaggedValue value)
{
    return Vertex::New<TaggedConstantVertex>(&chunk_, SAMPLE_VERTEX_ID, value.GetRawData());
}

CallRuntimeVertex *ArkSteedDceTest::NewRuntimeConsumer(const std::vector<ValueVertex *> &inputs)
{
    return NewVertex<CallRuntimeVertex>(inputs, RTSTUB_ID(NewTaggedArray), SideEffectKind::SAFE_CALL);
}

void ArkSteedDceTest::AttachLazyFrame(CallRuntimeVertex *consumer, ValueVertex *value)
{
    ASSERT_NE(consumer, nullptr);
    auto *state = chunk_.New<CallRuntimeVertex::LazyDeoptFrameState>(&chunk_);
    ASSERT_NE(state, nullptr);
    state->emplace_back(VRegIDType(FIRST_VREG), value, DeoptTranslationKind::TAGGED);
    consumer->SetLazyDeoptFrameState(state, SAMPLE_BYTECODE_OFFSET);
}

void ArkSteedDceTest::AttachCatchInputs(CallRuntimeVertex *consumer, ValueVertex *entry,
                                        const std::vector<ValueVertex *> &incoming)
{
    ASSERT_NE(consumer, nullptr);
    BB *block = BB::New(&chunk_);
    auto *control = NewVertex<ReturnVertex>({entry});
    ASSERT_NE(block, nullptr);
    ASSERT_NE(control, nullptr);
    SetControl(block, control);
    block->AddPredecessor(BB::New(&chunk_));
    uint32_t vreg = FIRST_VREG;
    for (ValueVertex *value : incoming) {
        auto *phi = NewVertex<PhiVertex>({entry, value}, VirtualRegister(vreg++));
        ASSERT_NE(phi, nullptr);
        phi->SetOwner(block);
        block->AddPhiVertex(phi);
    }
    consumer->LoadCatchBlock(block, CATCH_SITE_INDEX);
}

void ArkSteedDceTest::ExpectDependencies(Vertex *consumer, const std::vector<ValueVertex *> &expected)
{
    ASSERT_NE(consumer, nullptr);
    EXPECT_FALSE(consumer->HasId());
    std::vector<ValueVertex *> visited;
    DceDependencies::Visit(consumer, [&visited](ValueVertex *value) { visited.push_back(value); });
    EXPECT_EQ(visited, expected) << "dependency order mismatch: " << DescribePointerVector(visited);
}

void ArkSteedDceTest::CheckCombinedLazyAndCatchDependencies(ValueVertex *direct, ValueVertex *frame)
{
    ValueVertex *entry = NewTaggedSample(JSTaggedValue::False());
    ValueVertex *first = NewTaggedSample(JSTaggedValue::True());
    ValueVertex *second = NewTaggedSample(JSTaggedValue::Hole());
    ASSERT_NE(entry, nullptr);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    auto *consumer = NewRuntimeConsumer({direct});
    ASSERT_NE(consumer, nullptr);
    ASSERT_NO_FATAL_FAILURE(AttachLazyFrame(consumer, frame));
    ASSERT_NO_FATAL_FAILURE(AttachCatchInputs(consumer, entry, {first, second}));
    ExpectDependencies(consumer, {direct, frame, first, second});
}

void ArkSteedDceTest::CheckCombinedEagerDependencies(ValueVertex *frame)
{
    auto *input = Vertex::New<Int32ConstantVertex>(&chunk_, SAMPLE_VERTEX_ID, SAMPLE_INT32_VALUE);
    ASSERT_NE(input, nullptr);
    auto *consumer = NewVertex<I32AddWithOverflowVertex>({input, input}, &chunk_, SAMPLE_BYTECODE_OFFSET);
    ASSERT_NE(consumer, nullptr);
    I32AddWithOverflowVertex::EagerDeoptFrameState state(&chunk_);
    state.emplace_back(VRegIDType(FIRST_VREG), frame, DeoptTranslationKind::TAGGED);
    consumer->SetEagerDeoptFrameState(std::move(state));
    ExpectDependencies(consumer, {input, input, frame});
}

void ArkSteedDceTest::CheckAbsentLazyFrameContributesNoDependencies(ValueVertex *direct)
{
    auto *consumer = NewRuntimeConsumer({direct});
    ASSERT_NE(consumer, nullptr);
    EXPECT_FALSE(consumer->HasLazyDeoptFrameState());
    ExpectDependencies(consumer, {direct});
}

void ArkSteedDceTest::CheckIsolatedEagerDependency(ValueVertex *producer)
{
    auto *consumer = NewVertex<DeoptVertex>({}, &chunk_, kungfu::DeoptType::NONE, SAMPLE_BYTECODE_OFFSET);
    ASSERT_NE(consumer, nullptr);
    DeoptVertex::EagerDeoptFrameState state(&chunk_);
    state.emplace_back(VRegIDType(FIRST_VREG), producer, DeoptTranslationKind::TAGGED);
    consumer->SetEagerDeoptFrameState(std::move(state));
    ExpectDependencies(consumer, {producer});
}

void ArkSteedDceTest::CheckIsolatedLazyDependency(ValueVertex *producer)
{
    auto *consumer = NewRuntimeConsumer({});
    ASSERT_NE(consumer, nullptr);
    ASSERT_NO_FATAL_FAILURE(AttachLazyFrame(consumer, producer));
    ExpectDependencies(consumer, {producer});
}

void ArkSteedDceTest::CheckIsolatedCatchDependency(ValueVertex *producer)
{
    auto *entry = NewTaggedSample(JSTaggedValue::Hole());
    ASSERT_NE(entry, nullptr);
    auto *consumer = NewRuntimeConsumer({});
    ASSERT_NE(consumer, nullptr);
    ASSERT_NO_FATAL_FAILURE(AttachCatchInputs(consumer, entry, {producer}));
    ExpectDependencies(consumer, {producer});
}

void ArkSteedDceTest::CheckEditorOrder(const EditorFixture &fixture)
{
    const std::vector<NonControlVertex *> expectedEntry {fixture.liveProducer, fixture.parameter};
    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                   fixture.entry->GetVertices().end());
    EXPECT_EQ(entryAfter, expectedEntry) << "entry block order mismatch: " << DescribePointerVector(entryAfter);
    const std::vector<PhiVertex *> expectedPhis {fixture.livePhi};
    const std::vector<PhiVertex *> phiAfter(fixture.merge->GetPhis().begin(), fixture.merge->GetPhis().end());
    EXPECT_EQ(phiAfter, expectedPhis) << "Phi order mismatch: " << DescribePointerVector(phiAfter);
    ASSERT_EQ(fixture.livePhi->GetInputCount(), 2U);
    EXPECT_EQ(fixture.livePhi->GetInput(0), fixture.liveProducer);
    EXPECT_EQ(fixture.livePhi->GetInput(1), fixture.liveProducer);
    EXPECT_EQ(fixture.merge->GetControlVertex(), static_cast<ControlVertex *>(fixture.mergeReturn));
    EXPECT_EQ(fixture.entry->GetControlVertex(), static_cast<ControlVertex *>(fixture.branch));
    EXPECT_EQ(fixture.trueJump->GetPredecessorId(), 0U);
    EXPECT_EQ(fixture.falseJump->GetPredecessorId(), 1U);
}

void ArkSteedDceTest::CheckRemovedStorage(const EditorFixture &fixture)
{
    // Chunk objects remain readable, and filtering does not allocate ids.
    EXPECT_EQ(fixture.deadChainHead->GetOpcode(), VertexOpcode::I32Add);
    EXPECT_EQ(fixture.deadChainHead->GetInput(0), fixture.int32Constant);
    EXPECT_EQ(fixture.deadChainTail->GetOpcode(), VertexOpcode::I32ToTaggedInt);
    EXPECT_EQ(fixture.deadChainTail->GetInput(0), fixture.deadChainHead);
    EXPECT_EQ(fixture.deadConsumer->GetInput(0), fixture.liveProducer);
    EXPECT_EQ(fixture.deadSink->GetInput(0), fixture.liveProducer);
    EXPECT_EQ(fixture.deadPhi->GetInput(0), fixture.int32Constant);
    EXPECT_FALSE(fixture.deadChainHead->HasId());
    EXPECT_FALSE(fixture.liveProducer->HasId());
    EXPECT_FALSE(fixture.merge->GetControlVertex()->HasId());
}

void ArkSteedDceTest::BuildDummyLoop(Graph *&graph, BB *&header, BB *&latch, ValueVertex *&entryValue,
                                     I32AddVertex *&backedgeValue, PhiVertex *&headerPhi,
                                     ReturnVertex *&headerReturn, JumpLoopVertex *&latchBackedge)
{
    graph = Graph::New(&chunk_);
    BB *preheader = BB::New(&chunk_);
    header = BB::New(&chunk_);
    latch = BB::New(&chunk_);
    ASSERT_NE(graph, nullptr) << "loop fixture construction failed";
    ASSERT_NE(preheader, nullptr) << "loop fixture construction failed";
    ASSERT_NE(header, nullptr) << "loop fixture construction failed";
    ASSERT_NE(latch, nullptr) << "loop fixture construction failed";
    graph->Add(preheader);
    graph->Add(header);
    graph->Add(latch);
    entryValue = graph->GetInt32Constant(LOOP_ENTRY_VALUE);
    backedgeValue = NewVertex<I32AddVertex>({entryValue, entryValue});
    ASSERT_NE(entryValue, nullptr) << "loop fixture construction failed";
    ASSERT_NE(backedgeValue, nullptr) << "loop fixture construction failed";
    backedgeValue->SetOwner(latch);
    latch->AddVertex(backedgeValue);
    headerPhi = NewVertex<PhiVertex>({entryValue, backedgeValue}, VirtualRegister(0));
    headerReturn = NewVertex<ReturnVertex>({entryValue});
    JumpVertex *preheaderJump = NewVertex<JumpVertex>({}, header);
    latchBackedge = Vertex::New<JumpLoopVertex>(&chunk_, 0, &chunk_, header);
    ASSERT_NE(headerPhi, nullptr) << "loop fixture construction failed";
    ASSERT_NE(headerReturn, nullptr) << "loop fixture construction failed";
    ASSERT_NE(preheaderJump, nullptr) << "loop fixture construction failed";
    ASSERT_NE(latchBackedge, nullptr) << "loop fixture construction failed";
    headerPhi->SetOwner(header);
    header->AddPhiVertex(headerPhi);
    header->SetIsLoopHeader(true);
    headerReturn->SetOwner(header);
    SetControl(header, headerReturn);
    preheaderJump->SetOwner(preheader);
    SetControl(preheader, preheaderJump);
    latchBackedge->SetOwner(latch);
    SetControl(latch, latchBackedge);
    // Production order: preheader id 0, then structural dummy latch id 1.
    preheaderJump->SetPredecessorId(header->PredecessorCount());
    header->AddPredecessor(preheader);
    latchBackedge->SetPredecessorId(header->PredecessorCount());
    header->AddPredecessor(latch);
}

void ArkSteedDceTest::ConnectPhiCycle(Graph *graph, BB *entry, BB *merge, BB *exit, ValueVertex *constant,
                                      ReturnVertex *&exitReturn)
{
    JumpVertex *entryJump = NewVertex<JumpVertex>({}, merge);
    JumpVertex *mergeJump = NewVertex<JumpVertex>({}, exit);
    exitReturn = NewVertex<ReturnVertex>({constant});
    ASSERT_NE(entryJump, nullptr) << "cycle fixture construction failed";
    ASSERT_NE(mergeJump, nullptr) << "cycle fixture construction failed";
    ASSERT_NE(exitReturn, nullptr) << "cycle fixture construction failed";
    SetControl(entry, entryJump);
    SetControl(merge, mergeJump);
    SetControl(exit, exitReturn);
    entryJump->SetPredecessorId(merge->PredecessorCount());
    merge->AddPredecessor(entry);
    mergeJump->SetPredecessorId(exit->PredecessorCount());
    exit->AddPredecessor(merge);
    RunVerifier(graph);
}

void ArkSteedDceTest::AddRetainedDeoptState(const CatchFixture &fixture, I32AddWithOverflowVertex *&eagerSurvivor)
{
    ValueVertex *int32Constant = fixture.graph->GetInt32Constant(9);
    ASSERT_NE(int32Constant, nullptr) << "catch fixture construction failed";
    eagerSurvivor = NewVertex<I32AddWithOverflowVertex>({int32Constant, int32Constant}, &chunk_, 0);
    ASSERT_NE(eagerSurvivor, nullptr) << "catch fixture construction failed";
    eagerSurvivor->SetOwner(fixture.entry);
    I32AddWithOverflowVertex::EagerDeoptFrameState eagerState(&chunk_);
    eagerState.emplace_back(VRegIDType(0), fixture.first, DeoptTranslationKind::TAGGED);
    eagerSurvivor->SetEagerDeoptFrameState(std::move(eagerState));
    fixture.entry->AddVertex(eagerSurvivor);
    auto *lazyState = chunk_.New<CallRuntimeVertex::LazyDeoptFrameState>(&chunk_);
    ASSERT_NE(lazyState, nullptr) << "catch fixture construction failed";
    lazyState->emplace_back(VRegIDType(0), fixture.first, DeoptTranslationKind::TAGGED);
    fixture.firstThrowable->SetLazyDeoptFrameState(lazyState, 0);
}

void ArkSteedDceTest::CheckRetainedDeoptState(const CatchFixture &fixture, I32AddWithOverflowVertex *eagerSurvivor,
                                              PhiVertex *catchPhi)
{
    EXPECT_EQ(eagerSurvivor->GetDeoptFrameValueCount(), 1U);
    EXPECT_EQ(eagerSurvivor->GetDeoptFrameValue(0), fixture.first);
    EXPECT_TRUE(fixture.firstThrowable->HasLazyDeoptFrameState());
    EXPECT_EQ(fixture.firstThrowable->GetDeoptFrameValueCount(), 1U);
    EXPECT_EQ(fixture.firstThrowable->GetDeoptFrameValue(0), fixture.first);
    EXPECT_EQ(fixture.firstThrowable->GetCatchPredecessorIndex(), 0U);
    EXPECT_EQ(fixture.secondThrowable->GetCatchPredecessorIndex(), 1U);
    EXPECT_EQ(fixture.firstThrowable->GetCatchBlock(), fixture.catchBlock);
    ASSERT_EQ(catchPhi->GetInputCount(), 2U);
    EXPECT_EQ(catchPhi->GetInput(0), fixture.second);
    EXPECT_EQ(catchPhi->GetInput(1), fixture.first);
    EXPECT_EQ(fixture.catchBlock->GetControlVertex(), static_cast<ControlVertex *>(fixture.catchJump));
}

void ArkSteedDceTest::CheckRetainedExceptionalSite(const CatchFixture &fixture, PhiVertex *catchPhi)
{
    const std::vector<NonControlVertex *> entryBefore(fixture.entry->GetVertices().begin(),
                                                    fixture.entry->GetVertices().end());
    RemovalRequest narrow;
    narrow.MarkRemoved(fixture.firstThrowable);
    EXPECT_EQ(FilterRemovals(fixture.graph, &chunk_, narrow), DceResult::Unchanged);
    const std::vector<NonControlVertex *> entryAfter(fixture.entry->GetVertices().begin(),
                                                   fixture.entry->GetVertices().end());
    EXPECT_EQ(entryAfter, entryBefore);
    EXPECT_EQ(fixture.firstThrowable->GetCatchPredecessorIndex(), 0U);
    EXPECT_EQ(fixture.secondThrowable->GetCatchPredecessorIndex(), 1U);
    ASSERT_EQ(catchPhi->GetInputCount(), 2U);
}

}  // namespace panda::ecmascript::arksteed
