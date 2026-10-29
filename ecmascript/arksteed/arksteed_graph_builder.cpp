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

#include "ecmascript/arksteed/arksteed_graph_builder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <type_traits>

#include "securec.h"

#include "ecmascript/accessor_data.h"
#include "ecmascript/arksteed/arksteed_compile_info_facts.h"
#include "ecmascript/arksteed/arksteed_constant_folding.h"
#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_heap_broker.h"
#include "ecmascript/arksteed/arksteed_loop_effects.h"
#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/arksteed/arksteed_register_merge_state.h"
#include "ecmascript/arksteed/arksteed_side_effect_classifier.h"
#include "ecmascript/arksteed/arksteed_write_barrier_value_kind_pass.h"
#include "ecmascript/base/bit_helper.h"
#include "ecmascript/base/number_helper.h"
#include "ecmascript/builtin_entries.h"
#include "ecmascript/compiler/lazy_deopt_dependency.h"
#include "ecmascript/deoptimizer/deoptimizer.h"
#include "ecmascript/ecma_string-inl.h" // IWYU pragma: keep
#include "ecmascript/elements.h"
#include "ecmascript/global_env_constants.h"
#include "ecmascript/ic/ic_handler.h"
#include "ecmascript/ic/ic_info.h"
#include "ecmascript/ic/profile_type_info.h"
#include "ecmascript/ic/profile_type_info_cell.h"
#include "ecmascript/ic/property_box.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_arraybuffer.h"
#include "ecmascript/js_arguments.h"
#include "ecmascript/js_function.h"
#include "ecmascript/js_function_kind.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/js_typed_array.h"
#include "ecmascript/jspandafile/program_object.h"
#include "ecmascript/lexical_env.h"
#include "ecmascript/mem/mem.h"
#include "ecmascript/method.h"
#include "ecmascript/module/js_module_manager.h"
#include "ecmascript/string/base_string.h"

namespace panda::ecmascript::arksteed {

namespace {
constexpr uint32_t CALL_ARG0 = 0;
constexpr uint32_t CALL_ARG1 = 1;
constexpr uint32_t CALL_ARG2 = CALL_ARG1 + 1;
constexpr uint32_t CALL_ARG3 = CALL_ARG2 + 1;

using BuiltinId = kungfu::BuiltinsStubCSigns::ID;

#define COUNT_BUILTIN_CONSTRUCTOR(name) + 1U
constexpr uint32_t BUILTIN_CONSTRUCTOR_STUB_COUNT = 0U BUILTINS_CONSTRUCTOR_STUB_LIST(COUNT_BUILTIN_CONSTRUCTOR);
#undef COUNT_BUILTIN_CONSTRUCTOR

constexpr uint32_t BUILTIN_CONSTRUCTOR_STUB_FIRST_ID =
    static_cast<uint32_t>(BuiltinId::BUILTINS_CONSTRUCTOR_STUB_FIRST);
constexpr uint32_t BUILTIN_CONSTRUCTOR_STUB_LAST_ID =
    BUILTIN_CONSTRUCTOR_STUB_FIRST_ID + BUILTIN_CONSTRUCTOR_STUB_COUNT - 1U;

bool CanOptimizeBuiltinConstructor(BuiltinId id)
{
    uint32_t builtinId = static_cast<uint32_t>(id);
    if (builtinId >= BUILTIN_CONSTRUCTOR_STUB_FIRST_ID && builtinId <= BUILTIN_CONSTRUCTOR_STUB_LAST_ID) {
        return true;
    }
    return id == BuiltinId::BigIntConstructor || id == BuiltinId::ObjectConstructor ||
           id == BuiltinId::ErrorConstructor;
}

kungfu::DeoptType GetBuiltinConstructorDeoptType(uint32_t globalEnvFieldIndex)
{
    switch (globalEnvFieldIndex) {
        case GlobalEnv::ARRAY_FUNCTION_INDEX:
            return kungfu::DeoptType::NEWBUILTINCTORARRAY;
        case GlobalEnv::OBJECT_FUNCTION_INDEX:
            return kungfu::DeoptType::NEWBUILTINCTOROBJECT;
        case GlobalEnv::FLOAT32_ARRAY_FUNCTION_INDEX:
            return kungfu::DeoptType::NEWBUILTINCTORFLOAT32ARRAY;
        case GlobalEnv::BOOLEAN_FUNCTION_INDEX:
            return kungfu::DeoptType::NEWBUILTINCTORBOOLEAN;
        case GlobalEnv::BUILTINS_MAP_FUNCTION_INDEX:
            return kungfu::DeoptType::NEWBUILTINCTORMAP;
        case GlobalEnv::BUILTINS_SET_FUNCTION_INDEX:
            return kungfu::DeoptType::NEWBUILTINCTORSET;
        case GlobalEnv::DATE_FUNCTION_INDEX:
            return kungfu::DeoptType::NEWBUILTINCTORDATE;
        case GlobalEnv::ERROR_FUNCTION_INDEX:
            return kungfu::DeoptType::NEWBUILTINCTORERROR;
        default:
            return kungfu::DeoptType::NEWBUILTINCTORFAIL1;
    }
}

class KnownCallPlan {
public:
    static KnownCallPlan ForFeedback(uint32_t actualArgc, ValueVertex *expectedTargetConstant)
    {
        return KnownCallPlan(actualArgc, expectedTargetConstant);
    }

    uint32_t GetActualArgc() const
    {
        return actualArgc_;
    }

    ValueVertex *GetExpectedTargetConstant() const
    {
        return expectedTargetConstant_;
    }

    void Verify() const
    {
#ifndef NDEBUG
        ASSERT(expectedTargetConstant_ != nullptr);
        ASSERT(expectedTargetConstant_->Is<HeapConstantVertex>());
        ASSERT(expectedTargetConstant_->Cast<HeapConstantVertex>()->GetStaticNodeType() ==
               static_cast<uint16_t>(NodeInfo::NodeType::JS_FUNCTION));
#endif
    }

private:
    KnownCallPlan(uint32_t actualArgc, ValueVertex *expectedTargetConstant)
        : actualArgc_(actualArgc), expectedTargetConstant_(expectedTargetConstant)
    {
    }

    uint32_t actualArgc_;
    ValueVertex *expectedTargetConstant_;
};

class KnownBuiltinCallPlan {
public:
    KnownBuiltinCallPlan(uint32_t actualArgc, kungfu::BuiltinsStubCSigns::ID builtinId,
                         uint32_t globalEnvFieldIndex, BuiltinCallEntryKind entryKind,
                         ValueVertex *expectedMethodConstant)
        : actualArgc_(actualArgc), builtinId_(builtinId), globalEnvFieldIndex_(globalEnvFieldIndex),
          entryKind_(entryKind), expectedMethodConstant_(expectedMethodConstant)
    {
        ASSERT(expectedMethodConstant_ != nullptr);
    }

    uint32_t GetActualArgc() const
    {
        return actualArgc_;
    }

    kungfu::BuiltinsStubCSigns::ID GetBuiltinId() const
    {
        return builtinId_;
    }

    uint32_t GetGlobalEnvFieldIndex() const
    {
        return globalEnvFieldIndex_;
    }

    BuiltinCallEntryKind GetEntryKind() const
    {
        return entryKind_;
    }

    ValueVertex *GetExpectedMethodConstant() const
    {
        ASSERT(expectedMethodConstant_ != nullptr);
        return expectedMethodConstant_;
    }

private:
    uint32_t actualArgc_;
    kungfu::BuiltinsStubCSigns::ID builtinId_;
    uint32_t globalEnvFieldIndex_;
    BuiltinCallEntryKind entryKind_;
    ValueVertex *expectedMethodConstant_;
};

class BuiltinConstructPlan {
public:
    BuiltinConstructPlan(uint32_t actualArgc, BuiltinId builtinId, uint32_t globalEnvFieldIndex)
        : actualArgc_(actualArgc), builtinId_(builtinId), globalEnvFieldIndex_(globalEnvFieldIndex)
    {}

    uint32_t GetActualArgc() const
    {
        return actualArgc_;
    }

    BuiltinId GetBuiltinId() const
    {
        return builtinId_;
    }

    uint32_t GetGlobalEnvFieldIndex() const
    {
        return globalEnvFieldIndex_;
    }

    BuiltinCallEntryKind GetEntryKind() const
    {
        return BuiltinCallEntryKind::NATIVE_CPP;
    }

private:
    uint32_t actualArgc_;
    BuiltinId builtinId_;
    uint32_t globalEnvFieldIndex_;
};

class KnownConstructPlan {
public:
    KnownConstructPlan(uint32_t expectedArgc, ValueVertex *target, ValueVertex *hclass, uint32_t size,
                       bool canAllocateDirectly)
        : expectedArgc_(expectedArgc),
          target_(target),
          hclass_(hclass),
          size_(size),
          canAllocateDirectly_(canAllocateDirectly)
    {}
    uint32_t GetExpectedParameterCount() const
    {
        return expectedArgc_;
    }
    ValueVertex *GetExpectedTargetConstant() const
    {
        return target_;
    }
    ValueVertex *GetHClass() const
    {
        return hclass_;
    }
    uint32_t GetObjectSize() const
    {
        return size_;
    }
    bool CanAllocateDirectly() const
    {
        return canAllocateDirectly_;
    }

private:
    uint32_t expectedArgc_;
    ValueVertex *target_;
    ValueVertex *hclass_;
    uint32_t size_;
    bool canAllocateDirectly_;
};

bool IsTaggedCallType(kungfu::VariableType type)
{
    kungfu::GateType gateType = type.GetGateType();
    return gateType == kungfu::GateType::TaggedValue() || gateType == kungfu::GateType::TaggedPointer() ||
           gateType == kungfu::GateType::TaggedNPointer();
}

bool MatchesCallSignatureType(const ValueVertex *value, kungfu::VariableType type)
{
    if (IsTaggedCallType(type)) {
        return value->IsTagged();
    }

    switch (type.GetMachineType()) {
        case kungfu::MachineType::NOVALUE:
        case kungfu::MachineType::ANYVALUE:
        case kungfu::MachineType::FLEX:
            return true;
        case kungfu::MachineType::ARCH:
        case kungfu::MachineType::I64:
            return value->IsInt64();
        case kungfu::MachineType::I1:
        case kungfu::MachineType::I8:
        case kungfu::MachineType::I16:
        case kungfu::MachineType::I32:
            return value->IsInt32() || value->IsUInt32() || value->IsInt64();
        case kungfu::MachineType::F32:
        case kungfu::MachineType::F64:
            return value->IsAnyFloat64();
    }
    return true;
}

void ValidateCommonStubCallArgs(Span<ValueVertex *const> inputs, CommonStubID id)
{
#ifndef NDEBUG
    const kungfu::CallSignature *signature = kungfu::CommonStubCSigns::Get(id);
    size_t actualCount = inputs.size();
    size_t expectedCount = signature->GetParametersCount();
    if (actualCount != expectedCount) {
        LOG_ECMA(FATAL) << "ArkSteed CommonStub argument count mismatch, stub: " << signature->GetName()
                        << ", expected: " << expectedCount << ", actual: " << actualCount;
        UNREACHABLE();
    }
    kungfu::VariableType *params = signature->GetParametersType();
    if (params == nullptr) {
        return;
    }
    const auto *inputArr = inputs.begin();
    for (size_t i = 0; i < expectedCount; ++i) {
        if (MatchesCallSignatureType(inputArr[i], params[i])) {
            continue;
        }
        const char *reprName = ValueRepresentationName(inputArr[i]->GetValueRepresentation());
        LOG_ECMA(FATAL) << "ArkSteed CommonStub argument type mismatch, stub: " << signature->GetName()
                        << ", index: " << i
                        << ", expected machine type: " << MachineTypeToStr(params[i].GetMachineType())
                        << ", actual representation: " << reprName;
        UNREACHABLE();
    }
#else
    (void)inputs;  // // No-op in Release build
    (void)id;
#endif
}

void ValidateCommonStubCallArgs(std::initializer_list<ValueVertex *> inputs, CommonStubID id)
{
    auto span = Span<ValueVertex *const> {inputs.begin(), inputs.size()};
    ValidateCommonStubCallArgs(span, id);
}

bool SupportsI32CheckedBinOp(BinaryOpKind kind)
{
    switch (kind) {
        case BinaryOpKind::ADD:
        case BinaryOpKind::SUB:
        case BinaryOpKind::MUL:
        case BinaryOpKind::DIV:
        case BinaryOpKind::MOD:
            return true;
        default:
            return false;
    }
}

bool SupportsF64BinOp(BinaryOpKind kind)
{
    switch (kind) {
        case BinaryOpKind::ADD:
        case BinaryOpKind::SUB:
        case BinaryOpKind::MUL:
        case BinaryOpKind::DIV:
            return true;
        default:
            return false;
    }
}

bool IsReferenceComparableRootValue(ValueVertex *node)
{
    if (node == nullptr) {
        return false;
    }
    auto *constant = node->TryCast<TaggedConstantVertex>();
    if (constant == nullptr) {
        return false;
    }
    JSTaggedValue tagged(constant->GetValue());
    return tagged.IsBoolean() || tagged.IsNull() || tagged.IsUndefined() || tagged.IsHole();
}

bool IsNullishRootValue(ValueVertex *node)
{
    ASSERT(node != nullptr);
    auto *constant = node->TryCast<TaggedConstantVertex>();
    if (constant == nullptr) {
        return false;
    }
    JSTaggedValue tagged(constant->GetValue());
    return tagged.IsNull() || tagged.IsUndefined();
}

bool IsReferenceComparableType(NodeInfo::NodeType type)
{
    using NodeType = NodeInfo::NodeType;
    constexpr NodeType referenceComparable =
        NodeInfo::UnionNodeType(NodeInfo::UnionNodeType(NodeType::NULL_OR_UNDEFINED, NodeType::BOOLEAN),
                                NodeInfo::UnionNodeType(NodeType::SYMBOL, NodeType::JS_RECEIVER));
    return NodeInfo::NodeTypeIs(type, referenceComparable);
}

bool StrictTypesCanBeEqual(NodeInfo::NodeType leftType, NodeInfo::NodeType rightType)
{
    if (NodeInfo::NodeTypeCanBe(NodeInfo::IntersectNodeType(leftType, rightType), NodeInfo::NodeType::UNKNOWN)) {
        return true;
    }
    return NodeInfo::NodeTypeCanBe(leftType, NodeInfo::NodeType::NUMBER) &&
           NodeInfo::NodeTypeCanBe(rightType, NodeInfo::NodeType::NUMBER);
}

// ---- Common-subexpression elimination helpers (available expressions) ----
// Standard 64-bit FNV-1a constants. The hash is only an available-expression lookup key;
// CompileInfoFacts::FindExpression still checks opcode, inputs, options, and effect epoch.
constexpr uint64_t CSE_FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t CSE_FNV_PRIME = 1099511628211ULL;

uint64_t CseHashCombine(uint64_t hash, uint64_t value)
{
    hash ^= value;
    hash *= CSE_FNV_PRIME;
    return hash;
}

uint64_t CseHashValue(ValueVertex *value)
{
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
}

uint32_t CseHashExpression(VertexOpcode opcode, const CompileInfoFacts::ExpressionInputs &inputs,
                           const CompileInfoFacts::ExpressionOptions &options)
{
    uint64_t hash = CseHashCombine(CSE_FNV_OFFSET_BASIS, static_cast<uint64_t>(opcode));
    for (ValueVertex *input : inputs) {
        hash = CseHashCombine(hash, CseHashValue(input));
    }
    for (uint64_t option : options) {
        hash = CseHashCombine(hash, option);
    }
    return static_cast<uint32_t>(hash ^ (hash >> 32U));  // 32: fold 64-bit hash to 32-bit hash.
}

template <typename T>
void CseAppendExpressionOption(CompileInfoFacts::ExpressionOptions &options, const T &value)
{
    using RawT = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_enum_v<RawT>) {
        options.push_back(static_cast<uint64_t>(value));
    } else if constexpr (std::is_integral_v<RawT>) {
        options.push_back(static_cast<uint64_t>(value));
    } else if constexpr (std::is_pointer_v<RawT>) {
        options.push_back(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value)));
    } else if constexpr (std::is_floating_point_v<RawT>) {
        if constexpr (sizeof(RawT) == sizeof(uint32_t)) {
            options.push_back(base::bit_cast<uint32_t>(value));
        } else {
            options.push_back(base::bit_cast<uint64_t>(value));
        }
    } else {
        static_assert(std::is_trivially_copyable_v<RawT>, "Unsupported available-expression option type");
        static_assert(sizeof(RawT) <= sizeof(uint64_t), "Available-expression option is too large");
        uint64_t bits = 0;
        if (memcpy_s(&bits, sizeof(bits), &value, sizeof(value)) != EOK) {
            UNREACHABLE();
        }
        options.push_back(bits);
    }
}

template <typename... Args>
void CseBuildExpressionOptions(CompileInfoFacts::ExpressionOptions &options, const Args &...args)
{
    (CseAppendExpressionOption(options, args), ...);
}

constexpr bool CseIsExcludedAvailableExpressionOpcode(VertexOpcode opcode)
{
    switch (opcode) {
        case VertexOpcode::Int32Constant:
        case VertexOpcode::Int64Constant:
        case VertexOpcode::Float64Constant:
        case VertexOpcode::TaggedConstant:
        case VertexOpcode::HeapConstant:
        case VertexOpcode::InitialValue:
        case VertexOpcode::ActualArgc:
        case VertexOpcode::Call:
        case VertexOpcode::CallRuntime:
        case VertexOpcode::CallCommonStub:
        case VertexOpcode::Deopt:
        case VertexOpcode::Phi:
            return true;
        default:
            return false;
    }
}

template <typename VertexT>
constexpr bool CseCanUseAvailableExpression()
{
    if constexpr (!std::is_base_of_v<ValueVertex, VertexT>) {
        return false;
    } else {
        return !CseIsExcludedAvailableExpressionOpcode(OpcodeOf<VertexT>) && CanParticipateInCSE(VertexT::PROPERTIES);
    }
}
}  // namespace

// Condensed storage: [vA, vA, vA, vB, vB, vB, vB, vB, vB, vC, vC, vC, vC]
//                 => [(vA, 3),    (vB, 6),                (vC, 4)]
struct GraphBuilder::CatchBlockInputData {
    struct InputEntry {
        ValueVertex *vertex;
        uint32_t count;
    };

    uint32_t totalCount;
    const kungfu::BitSet *liveIn;
    // inputs[i] = List of inputs for the i-th live-in virtual register
    ChunkVector<ChunkVector<InputEntry>> inputs;
    // Merge from all the vertices whose exceptions may be caught here
    CompileInfoFacts *facts;

    explicit CatchBlockInputData(const kungfu::BitSet &liveIn, Chunk *chunk)
        : totalCount(0), liveIn(&liveIn), inputs(chunk), facts(nullptr)
    {
        size_t numLive = liveIn.Count();
        inputs.reserve(numLive);
        for (size_t i = 0; i < numLive; i++) {
            inputs.emplace_back(chunk);
        }
    }

    // Returns the index of this catch predecessor
    uint32_t AddCatchPredecessor(const SharedBCFrameState frameState, ValueVertex *alt)
    {
        VRegIDType liveIndex = 0;
        // -1: Skips acc, which will be overwritten by the exception object
        for (VRegIDType i = 0, n = frameState.NumVRegs(); i < n - 1; i++) {
            if (!liveIn->TestBit(i)) {
                continue;
            }
            ValueVertex *value = frameState.Get(i);
            if (value == nullptr) {
                value = alt;
            }
            auto &curInputList = inputs[liveIndex++];
            if (curInputList.empty() || curInputList.back().vertex != value) {
                curInputList.push_back({.vertex = value, .count = 1});
            } else {
                curInputList.back().count += 1;
            }
        }
        uint32_t curInputIndex = totalCount;
        totalCount += 1;
        return curInputIndex;
    }

    void AddCompileInfoFacts(const CompileInfoFacts &incoming)
    {
        if (facts == nullptr) {
            facts = incoming.Clone();
            return;
        }
        facts->Merge(incoming);
    }
};

GraphBuilder::GraphBuilder(JSThread *compilerThread,
                           Graph *destGraph,
                           uintptr_t glueAddr,
                           BytecodePreprocessor *preproc,
                           BytecodeAnalysis *analysis,
                           bool isOsr)
    : graph_(destGraph),
      compilerThread_(compilerThread),
      glueAddr_(glueAddr),
      preproc_(preproc),
      analysis_(analysis),
      pgoContext_(compilerThread, preproc->GetEnv()),
      numLocal_(preproc->GetNumLocalVRegs()),
      numParams_(preproc->GetNumParamVRegs()),
      isOsr_(isOsr),
      chunk_(preproc->GetChunk()),
      blocks_(preproc->GetNumLiveBasicBlocks(), preproc->GetChunk()),
      exitBlocks_(preproc->GetNumLiveBasicBlocks(), preproc->GetChunk()),
      compileInfoFacts_(preproc->GetNumLiveBasicBlocks(), preproc->GetChunk()),
      catchBlockInputs_(preproc->GetNumLiveBasicBlocks(), preproc->GetChunk()),
      outgoingBlockStates_(preproc->GetChunk()),
      loopHeadersToPeel_(preproc->GetNumLiveBasicBlocks(), preproc->GetChunk()),
      handledSourceBlocks_(preproc->GetNumLiveBasicBlocks(), preproc->GetChunk()),
      loopEffectsStack_(preproc->GetChunk()),
      loopExitEdges_(preproc->GetChunk())
{}

bool GraphBuilder::Run()
{
    ASSERT(preproc_->GetNumLiveBasicBlocks() > 0);
    DebugLog();
    InitializeLoopPeeling();

    SharedBCFrameState frameState(preproc_->GetNumVRegs(), nullptr, chunk_);
    InitializeStartBlock(frameState);
    RecordOutgoingBlockState(0, frameState);

    // 1 : Skips the start block (which is initialized before)
    for (uint32_t i = 1, n = preproc_->GetNumLiveBasicBlocks(); i < n; i++) {
        if (handledSourceBlocks_[i] != 0) {
            continue;
        }
        if (loopHeadersToPeel_[i] && blocks_[i] != nullptr) {
            BuildPeeledLoop(preproc_->TryGetLoopInfo(preproc_->GetBasicBlockByRPO(i)));
            continue;
        }
        if (blocks_[i] == nullptr) {
            ProcessDeadBasicBlock(i);
            continue;
        }
        frameState.Reset(nullptr);
        ProcessBasicBlock(frameState, i);
        RecordOutgoingBlockState(i, frameState);
    }
    SynchronizeLoopEffects(nullptr);
    return true;
}

BB *&GraphBuilder::BlockAt(uint32_t rpoIndex)
{
    return blocks_[rpoIndex];
}

BB *&GraphBuilder::ExitBlockAt(uint32_t rpoIndex)
{
    return exitBlocks_[rpoIndex];
}

CompileInfoFacts *&GraphBuilder::CompileInfoFactsAt(uint32_t rpoIndex)
{
    return compileInfoFacts_[rpoIndex];
}

const GraphBuilder::OutgoingBlockState &GraphBuilder::GetOutgoingBlockState(BB *block) const
{
    auto it = outgoingBlockStates_.find(block);
    ASSERT(it != outgoingBlockStates_.end());
    return it->second;
}

GraphBuilder::CatchBlockInputData *&GraphBuilder::CatchBlockInputsAt(uint32_t rpoIndex)
{
    return catchBlockInputs_[rpoIndex];
}

void GraphBuilder::BeginLoopEffects(const BytecodeLoopInfo *loop)
{
    BB *header = BlockAt(loop->header->rpoIndex);
    LoopEffects *effects = header == nullptr ? nullptr : header->GetLoopEffects();
    if (effects == nullptr) {
        effects = chunk_->New<LoopEffects>(chunk_);
    }
    loopEffectsStack_.push_back(LoopEffectScope {loop, effects});
}

LoopEffects *GraphBuilder::EndLoopEffects()
{
    ASSERT(!loopEffectsStack_.empty());
    LoopEffectScope scope = loopEffectsStack_.back();
    loopEffectsStack_.pop_back();
    BB *header = BlockAt(scope.source->header->rpoIndex);
    if (header != nullptr && header->IsLoopHeader()) {
        header->SetLoopEffects(scope.effects);
    }
    if (LoopEffects *outer = CurrentLoopEffects()) {
        outer->Merge(*scope.effects);
    }
    return scope.effects;
}

void GraphBuilder::SynchronizeLoopEffects(const BytecodeLoopInfo *loop)
{
    while (!loopEffectsStack_.empty()) {
        const BytecodeLoopInfo *parent = loop;
        while (parent != nullptr && parent != loopEffectsStack_.back().source) {
            parent = parent->parent;
        }
        if (parent != nullptr) {
            break;
        }
        EndLoopEffects();
    }
    if (loop != nullptr && (loopEffectsStack_.empty() || loopEffectsStack_.back().source != loop)) {
        SynchronizeLoopEffects(loop->parent);
        BeginLoopEffects(loop);
    }
}

void GraphBuilder::InitializeLoopPeeling()
{
    constexpr uint32_t MAX_LOOP_SIZE = 400;
    constexpr uint32_t MAX_CUMULATIVE_SIZE = 900;
    uint64_t cumulativeSize = 0;
    for (const BytecodeLoopInfo *loop : preproc_->GetLoopInfos()) {
        uint32_t loopSize = loop->BytecodeSize();
        if (!IsEligibleForLoopPeeling(loop) || loopSize >= MAX_LOOP_SIZE ||
            cumulativeSize + loopSize >= MAX_CUMULATIVE_SIZE) {
            continue;
        }

        loopHeadersToPeel_[loop->header->rpoIndex] = 1;
        cumulativeSize += loopSize;
    }
}

bool GraphBuilder::IsEligibleForLoopPeeling(const BytecodeLoopInfo *loop) const
{
    if (!loop->innermost || loop->resumable || isOsr_) {
        return false;
    }
    // CanonicalizeLoopsDFS has already merged multiple entries and backedges.
    ASSERT(loop->header != nullptr && loop->preheader != nullptr && loop->latch != nullptr);
    ASSERT(!loop->members.empty());
    ASSERT(loop->header->jumpPredecessors.size() == 2);  // 2: loop entry and latch backedge
    ASSERT(loop->latch->jumpBlock == loop->header);
    ASSERT(loop->preheader->rpoIndex < loop->header->rpoIndex);
    uint32_t firstBytecodeSize = static_cast<uint32_t>(preproc_->GetBytecode(loop->header->startBcIndex)->GetSize());
    return loop->loopStartOffset + firstBytecodeSize < loop->loopEndOffset;
}

void GraphBuilder::RecordOutgoingBlockState(uint32_t rpoIndex, SharedBCFrameState frameState)
{
    BB *exit = ExitBlockAt(rpoIndex);
    if (exit == nullptr || CompileInfoFactsAt(rpoIndex) == nullptr) {
        return;
    }
    CondensedBCFrameState condensed(frameState, analysis_->GetLiveOutOfBlock(rpoIndex), chunk_);
    outgoingBlockStates_[exit] = OutgoingBlockState {
        .frameState = condensed,
        .facts = CompileInfoFactsAt(rpoIndex),
    };
}

void GraphBuilder::FinalizeLoopExitEdges(uint32_t sourceRpoIndex)
{
    BB *sourceExit = ExitBlockAt(sourceRpoIndex);
    if (sourceExit == nullptr) {
        return;
    }
    const OutgoingBlockState &outgoing = GetOutgoingBlockState(sourceExit);
    for (const LoopExitEdge &edge : loopExitEdges_) {
        if (edge.sourceRpoIndex != sourceRpoIndex) {
            continue;
        }
        ASSERT(edge.landing->GetControlVertex() == nullptr);
        FinishBlockWithJump(edge.landing, edge.target);
        outgoingBlockStates_[edge.landing] = outgoing;
    }
}

BB *GraphBuilder::GetOrCreateLoopExitLanding(uint32_t sourceRpoIndex, uint32_t targetRpoIndex, BB *target)
{
    ASSERT(activePeeledLoop_ != nullptr && target != nullptr);
    const BasicBlockInfo *source = preproc_->GetBasicBlockByRPO(sourceRpoIndex);
    if (!source->IsConditionalJump()) {
        return target;
    }
    auto existing = std::find_if(loopExitEdges_.begin(), loopExitEdges_.end(),
                                 [sourceRpoIndex, targetRpoIndex](const LoopExitEdge &edge) {
                                     return edge.sourceRpoIndex == sourceRpoIndex &&
                                            edge.targetRpoIndex == targetRpoIndex;
                                 });
    if (existing != loopExitEdges_.end()) {
        ASSERT(existing->target == target);
        return existing->landing;
    }

    BB *landing = NewBlock();
    loopExitEdges_.push_back(LoopExitEdge {sourceRpoIndex, targetRpoIndex, landing, target});
    return landing;
}

void GraphBuilder::ResetLoopBuildState(const BytecodeLoopInfo *loop)
{
    // Already emitted blocks and outgoing states remain alive. Only the source-to-build
    // lookup is reset, including internal catch merges; exits outside the loop are shared.
    for (const BasicBlockInfo *block : loop->members) {
        uint32_t index = block->rpoIndex;
        blocks_[index] = nullptr;
        exitBlocks_[index] = nullptr;
        compileInfoFacts_[index] = nullptr;
        catchBlockInputs_[index] = nullptr;
    }
    loopExitEdges_.clear();
}

void GraphBuilder::BuildPeeledLoop(const BytecodeLoopInfo *loop)
{
    ASSERT(loop != nullptr && activePeeledLoop_ == nullptr);
    SynchronizeLoopEffects(loop->parent);
    activePeeledLoop_ = loop;
    loopPeelingStage_ = LoopPeelingStage::PEELED_ITERATION;
    SharedBCFrameState frameState(preproc_->GetNumVRegs(), nullptr, chunk_);
    do {
        nextLoopHeader_ = nullptr;
        optimisticHeaderFacts_ = nullptr;
        BeginLoopEffects(loop);
        for (const BasicBlockInfo *block : loop->members) {
            uint32_t index = block->rpoIndex;
            if (BlockAt(index) == nullptr) {
                if (loopPeelingStage_ == LoopPeelingStage::CONSERVATIVE_LOOP) {
                    ProcessDeadBasicBlock(index);
                }
                continue;
            }
            frameState.Reset(nullptr);
            ProcessBasicBlock(frameState, index);
            RecordOutgoingBlockState(index, frameState);
            FinalizeLoopExitEdges(index);
        }
        LoopEffects *effects = EndLoopEffects();
        if (nextLoopHeader_ == nullptr) {
            // A compatible backedge closed the loop, or all paths exited.
            if (loopPeelingStage_ == LoopPeelingStage::OPTIMISTIC_LOOP &&
                BlockAt(loop->header->rpoIndex)->IsLoopHeader() &&
                BlockAt(loop->header->rpoIndex)->PredecessorCount() == 1) {
                TurnOptimisticLoopIntoPeeledIteration();
            }
            break;
        }
        ASSERT(loopPeelingStage_ == LoopPeelingStage::PEELED_ITERATION ||
               loopPeelingStage_ == LoopPeelingStage::OPTIMISTIC_LOOP);
        if (loopPeelingStage_ == LoopPeelingStage::PEELED_ITERATION) {
            peeledLoopEffects_ = effects;
        }
        ResetLoopBuildState(loop);
        BlockAt(loop->header->rpoIndex) = nextLoopHeader_;
        loopPeelingStage_ = loopPeelingStage_ == LoopPeelingStage::PEELED_ITERATION
            ? LoopPeelingStage::OPTIMISTIC_LOOP : LoopPeelingStage::CONSERVATIVE_LOOP;
    } while (true);

    for (const BasicBlockInfo *block : loop->members) {
        handledSourceBlocks_[block->rpoIndex] = 1;
    }
    loopExitEdges_.clear();
    activePeeledLoop_ = nullptr;
    loopPeelingStage_ = LoopPeelingStage::NONE;
    peeledLoopEffects_ = nullptr;
    optimisticHeaderFacts_ = nullptr;
    nextLoopHeader_ = nullptr;
    currentSourceRpoIndex_ = BytecodePreprocessor::NULL_INDEX;
}

bool GraphBuilder::IsOptimisticBackedgeCompatible(SharedBCFrameState frameState, CompileInfoFacts *facts) const
{
    ASSERT(activePeeledLoop_ != nullptr && optimisticHeaderFacts_ != nullptr && facts != nullptr);
    BB *header = blocks_[activePeeledLoop_->header->rpoIndex];
    if (!facts->IsCompatibleWithLoopHeader(*optimisticHeaderFacts_, header)) {
        return false;
    }
    for (PhiVertex *phi : header->GetPhis()) {
        NodeInfo::NodeType expected = optimisticHeaderFacts_->GetKnownType(phi);
        ValueVertex *backedgeValue = frameState.Get(phi->GetOwner().GetId());
        NodeInfo::NodeType actual = facts->GetKnownType(backedgeValue);
        if (!NodeInfo::NodeTypeIs(actual, expected)) {
            return false;
        }
    }
    return true;
}

void GraphBuilder::TurnOptimisticLoopIntoPeeledIteration()
{
    ASSERT(activePeeledLoop_ != nullptr && loopPeelingStage_ == LoopPeelingStage::OPTIMISTIC_LOOP);
    BB *header = BlockAt(activePeeledLoop_->header->rpoIndex);
    header->SetIsLoopHeader(false);
    header->SetLoopEffects(nullptr);
    for (PhiVertex *phi : header->GetPhis()) {
        phi->ReduceToSinglePredecessor();
    }
}

void GraphBuilder::FinishLoopBackedge(BB *owner, SharedBCFrameState frameState, CompileInfoFacts *facts,
                                      uint32_t targetRpoIndex)
{
    if (activePeeledLoop_ == nullptr) {
        FinishBlockWithJumpLoop(owner, ActivateNonCatchBlock(targetRpoIndex));
        return;
    }
    ASSERT(activePeeledLoop_->header->rpoIndex == targetRpoIndex);
    if (loopPeelingStage_ == LoopPeelingStage::PEELED_ITERATION ||
        (loopPeelingStage_ == LoopPeelingStage::OPTIMISTIC_LOOP &&
         !IsOptimisticBackedgeCompatible(frameState, facts))) {
        if (loopPeelingStage_ == LoopPeelingStage::OPTIMISTIC_LOOP) {
            TurnOptimisticLoopIntoPeeledIteration();
        }
        nextLoopHeader_ = NewBlock();
        FinishBlockWithJump(owner, nextLoopHeader_);
        return;
    }
    FinishBlockWithJumpLoop(owner, BlockAt(targetRpoIndex));
}

void GraphBuilder::DebugLog()
{
    if (!common::Log::LogIsLoggable(Level::DEBUG, Component::COMPILER)) {
        return;
    }
    LOG_COMPILER(DEBUG) << "arksteed::GraphBuilder: Starts graph building with "
                           "NumLocalVRegs = "
                        << numLocal_ << ", NumParamVRegs = " << numParams_;

    std::string dumpStr = preproc_->Dump();
    std::istringstream preprocStream(dumpStr);
    std::string line;
    while (std::getline(preprocStream, line)) {
        LOG_COMPILER(DEBUG) << line;
    }

    dumpStr = analysis_->Dump();
    std::istringstream analysisStream(dumpStr);
    while (std::getline(analysisStream, line)) {
        LOG_COMPILER(DEBUG) << line;
    }
}

void GraphBuilder::InitializeStartBlock(SharedBCFrameState frameState)
{
    glue_ = graph_->GetIntPtrConstant(glueAddr_);
    undefinedValue_ = graph_->GetTaggedConstant(JSTaggedValue::VALUE_UNDEFINED);

    compileInfoFacts_[0] = chunk_->New<CompileInfoFacts>(chunk_);
    compileInfoFacts_[0]->EnsureType(undefinedValue_, NodeInfo::NodeType::UNDEFINED);

    blocks_[0] = BB::New(chunk_);
    // caller argument area (in fp-slot words): +2 argc, +3 call-target, +4 new-target, +5 this, +6... user args.
    const int32_t ACTUAL_ARGC_FP_SLOT_INDEX = 2;
    const int32_t CALL_TARGET_FP_SLOT_INDEX = 3;
    initialActualArgc_ = NewVertex<InitialValueVertex>(blocks_[0], {}, ACTUAL_ARGC_FP_SLOT_INDEX);
    for (uint32_t i = 0, n = numParams_; i < n; i++) {
        int32_t slotIndex = static_cast<int32_t>(i + CALL_TARGET_FP_SLOT_INDEX);
        auto *v = NewVertex<InitialValueVertex>(blocks_[0], {}, slotIndex);
        graph_->AddParameter(v);
        frameState.Set(VRegOfParam(numLocal_, i), v);
        if (i == CALL_TARGET_PARAM_INDEX) {
            ASSERT(slotIndex == CALL_TARGET_FP_SLOT_INDEX);
            initialCallTarget_ = v;
        }
    }
    ASSERT(initialCallTarget_ != nullptr);
    actualArgc_ = NewVertex<ActualArgcVertex>(blocks_[0], {});
    taggedActualArgc_ = NewVertex<I32ToTaggedIntVertex>(blocks_[0], {actualArgc_});

    // -3 : Fixed header lexicalEnv is at slot -3 in word units.
    initialLexicalEnv_ = NewVertex<InitialValueVertex>(blocks_[0], {}, -3);
    frameState.SetLexicalEnv(initialLexicalEnv_);
    if (!isOsr_ && GetOptions()->GetCompilerArkSteedPrintMethodName()) {
        // E1 execution marker: emit a single CallRuntime PrintMethodName vertex in the
        // method entry block for ordinary (non-OSR) compilations. The argument is the
        // existing function-object value (initialCallTarget_) with no extra countable
        // operations; the stub logs `>>> Running method: <record>.<method>` on each
        // activation of the generated code.
        NewVertex<CallRuntimeVertex>(blocks_[0], {initialCallTarget_}, RTSTUB_ID(PrintMethodName),
                                     SideEffectKind::SAFE_CALL);
    }
    FinishBlockWithJump(blocks_[0], ActivateNonCatchBlock(1));
    exitBlocks_[0] = blocks_[0];
}

void GraphBuilder::ProcessDeadBasicBlock(uint32_t rpoIndex)
{
    const BasicBlockInfo *bcBlock = preproc_->GetBasicBlockByRPO(rpoIndex);
    if (!bcBlock->IsEndOfLoop()) {
        LOG_COMPILER(DEBUG) << "Skips block #" << rpoIndex << " which is dead.";
        return;
    }
    uint32_t headerRpoIndex = bcBlock->jumpBlock->rpoIndex;
    if (BlockAt(headerRpoIndex) == nullptr) {
        LOG_COMPILER(DEBUG) << "Skips block #" << rpoIndex << " which is dead.";
        return;
    }
    // For the rare case where the loop-back block is dead (due to pattern like if (true) break),
    // a dummy basic block is created, which simply jumps the loop header.
    // Uses JumpLoopVertex so that LivenessProcessor can pop the loop from loopUsedVertices_.
    BlockAt(rpoIndex) = BB::New(chunk_);
    FinishDeadLoopBackEdge(BlockAt(rpoIndex), rpoIndex);
    ExitBlockAt(rpoIndex) = BlockAt(rpoIndex);
}

void GraphBuilder::FinishDeadLoopBackEdge(BB *owner, uint32_t rpoIndex)
{
    const BasicBlockInfo *bcBlock = preproc_->GetBasicBlockByRPO(rpoIndex);
    ASSERT(bcBlock->IsEndOfLoop());
    uint32_t headerRpoIndex = bcBlock->jumpBlock->rpoIndex;
    ASSERT(BlockAt(headerRpoIndex) != nullptr);
    FinishBlockWithJumpLoop(owner, BlockAt(headerRpoIndex));

    for (PhiVertex *phi : BlockAt(headerRpoIndex)->GetPhis()) {
        ASSERT(phi->GetInputCount() == 2);  // 2 : Two jumpPredecessors: one is entry, the other is loop-back
        phi->SetInput(1, undefinedValue_);
    }
}

void GraphBuilder::ProcessBasicBlock(SharedBCFrameState frameState, uint32_t rpoIndex)
{
    const BasicBlockInfo *bcBlock = preproc_->GetBasicBlockByRPO(rpoIndex);
    currentSourceRpoIndex_ = rpoIndex;
    bool isAcyclicLoopCopy = activePeeledLoop_ != nullptr && loopPeelingStage_ == LoopPeelingStage::PEELED_ITERATION;
    if (activePeeledLoop_ == nullptr) {
        const BasicBlockInfo *header = bcBlock->IsLoopHeader() ? bcBlock : bcBlock->loopHeaderBlock;
        SynchronizeLoopEffects(preproc_->TryGetLoopInfo(header));
    }
    if (bcBlock->IsCatchBlockHeader()) {
        ProcessCatchBlockHead(frameState, rpoIndex);
        return;
    }
    InitCompileInfoFacts(rpoIndex);
    if (bcBlock->IsLoopHeader() && !isAcyclicLoopCopy) {
        BlockAt(rpoIndex)->SetIsLoopHeader(true);
        BlockAt(rpoIndex)->SetHasPeeledIteration(activePeeledLoop_ != nullptr);
    }
    (bcBlock->IsLoopHeader() && !isAcyclicLoopCopy)
        ? InitFrameStateForLoopHeader(frameState, rpoIndex)
        : InitFrameState(frameState, rpoIndex);

    if (bcBlock->IsLoopHeader() && loopPeelingStage_ == LoopPeelingStage::OPTIMISTIC_LOOP) {
        optimisticHeaderFacts_ = CompileInfoFactsAt(rpoIndex)->Clone();
    }

    if (bcBlock->IsSynthetic()) {
        // Edge-split block, etc. No bytecode inside.
        if (bcBlock->IsEndOfLoop()) {
            FinishLoopBackedge(BlockAt(rpoIndex), frameState, CompileInfoFactsAt(rpoIndex),
                               bcBlock->jumpBlock->rpoIndex);
        } else {
            BB *target = ActivateNonCatchBlock(bcBlock->jumpBlock->rpoIndex);
            FinishBlockWithJump(BlockAt(rpoIndex), target);
        }
        ExitBlockAt(rpoIndex) = BlockAt(rpoIndex);
    } else {
        ExitBlockAt(rpoIndex) = VisitBytecodesOfBasicBlock(frameState, rpoIndex, &CompileInfoFactsAt(rpoIndex));
    }
    if (bcBlock->IsEndOfLoop()) {
        ControlVertex *control = ExitBlockAt(rpoIndex)->GetControlVertex();
        if (control->Is<DeoptVertex>() && !isAcyclicLoopCopy) {
            // An optimistic loop without a real backedge becomes a peeled iteration.
            // Other loops still need their structural backedge for phis and liveness.
            if (loopPeelingStage_ != LoopPeelingStage::OPTIMISTIC_LOOP) {
                FinishDeadLoopBackEdge(BB::New(chunk_), rpoIndex);
            }
        } else if (control->Is<JumpLoopVertex>()) {
            WriteBackFrameStateToLoopHeader(frameState, rpoIndex);
        } else {
            ASSERT(isAcyclicLoopCopy || (activePeeledLoop_ != nullptr &&
                   loopPeelingStage_ == LoopPeelingStage::OPTIMISTIC_LOOP && nextLoopHeader_ != nullptr));
        }
    }
}

void GraphBuilder::ProcessCatchBlockHead(SharedBCFrameState frameState, uint32_t rpoIndex)
{
    BlockAt(rpoIndex)->SetIsExceptionHandler(true);
    InitCompileInfoFactsForCatchBlock(rpoIndex);
    InitFrameStateForCatchBlockHeader(frameState, rpoIndex);

    const BasicBlockInfo *bcBlock = preproc_->GetBasicBlockByRPO(rpoIndex);
    ASSERT(bcBlock->jumpBlock != nullptr && !bcBlock->jumpBlock->IsSynthetic());

    // Catch block header is always synthetic. Only an unconditional jump.
    ASSERT(bcBlock->IsJump());
    FinishBlockWithJump(BlockAt(rpoIndex), ActivateNonCatchBlock(bcBlock->jumpBlock->rpoIndex));
    ExitBlockAt(rpoIndex) = BlockAt(rpoIndex);
}

void GraphBuilder::InitFrameState(SharedBCFrameState frameState, uint32_t rpoIndex)
{
    BB *target = BlockAt(rpoIndex);
    uint32_t actualNumPreds = target->PredecessorCount();
    for (uint32_t actualPredIndex = 0; actualPredIndex < actualNumPreds; ++actualPredIndex) {
        BB *predecessor = target->GetPredecessor(actualPredIndex);
        MergeFrameState(frameState, rpoIndex, GetOutgoingBlockState(predecessor), actualPredIndex, actualNumPreds);
    }
}

void GraphBuilder::InitFrameStateForLoopHeader(SharedBCFrameState frameState, uint32_t rpoIndex)
{
    const BasicBlockInfo *blockInfo = preproc_->GetBasicBlockByRPO(rpoIndex);
    ASSERT(blockInfo->jumpPredecessors.size() == 2);  // 2 : One is entry, the other is loop-back

    kungfu::BitSet phiCandidates(chunk_, frameState.NumVRegs());
    phiCandidates.CopyFrom(analysis_->GetLiveInOfBlock(rpoIndex));
    phiCandidates.Intersect(analysis_->GetKillSetOfBlock(rpoIndex));

    for (uint32_t vregIndex = 0, n = frameState.NumVRegs(); vregIndex < n; vregIndex++) {
        if (phiCandidates.TestBit(vregIndex)) {
            // 2 : One is entry, the other is loop-back
            frameState.Set(vregIndex, NewPhiVertex(BlockAt(rpoIndex), 2, vregIndex));
        }
    }
    BB *header = BlockAt(rpoIndex);
    ASSERT(header->PredecessorCount() == 1);
    // 0 : The first predecessor which is loop entry; 2 : Two jumpPredecessors, one is entry, the other is loop-back
    MergeFrameState(frameState, rpoIndex, GetOutgoingBlockState(header->GetPredecessor(0)), 0, 2);

    CompileInfoFacts *facts = CompileInfoFactsAt(rpoIndex);
    if (activePeeledLoop_ != nullptr && loopPeelingStage_ == LoopPeelingStage::OPTIMISTIC_LOOP) {
        for (PhiVertex *phi : header->GetPhis()) {
            facts->EnsureType(phi, facts->GetKnownType(phi->GetInput(0)));
        }
    }
}

void GraphBuilder::InitFrameStateForCatchBlockHeader(SharedBCFrameState frameState, uint32_t rpoIndex)
{
    const BasicBlockInfo *bcBlock = preproc_->GetBasicBlockByRPO(rpoIndex);
    // For catch blocks, acc is always initialized as the exception object
    frameState.SetAcc(NewVertex<LoadExceptionVertex>(BlockAt(rpoIndex), {glue_}));

    CatchBlockInputData *data = CatchBlockInputsAt(rpoIndex);
    ASSERT(data != nullptr);
    ASSERT(data->totalCount >= 1);

    const kungfu::BitSet &liveIn = analysis_->GetLiveInOfBlock(rpoIndex);
    VRegIDType liveIndex = 0;
    // -1: Skips acc, which is numbered as the last
    for (VRegIDType vregIndex = 0, n = frameState.NumVRegs(); vregIndex < n - 1; vregIndex++) {
        if (!liveIn.TestBit(vregIndex)) {
            continue;
        }
        const auto &curInputList = data->inputs[liveIndex++];
        ASSERT(!curInputList.empty());

        if (curInputList.size() == 1) {
            // Same inputs
            frameState.Set(vregIndex, curInputList[0].vertex);
            continue;
        }
        PhiVertex *phi = NewPhiVertex(BlockAt(rpoIndex), data->totalCount, vregIndex);
        uint32_t inputIndex = 0;
        for (const auto &[input, count] : curInputList) {
            // Decompress input list
            for (uint32_t j = 0; j < count; j++) {
                phi->SetInput(inputIndex++, input);
            }
        }
        frameState.Set(vregIndex, phi);
    }
}

void GraphBuilder::InitCompileInfoFacts(uint32_t rpoIndex)
{
    const BasicBlockInfo *blockInfo = preproc_->GetBasicBlockByRPO(rpoIndex);
    ASSERT(!blockInfo->IsCatchBlockHeader() && "Use InitCompileInfoFactsForCatchBlock() instead.");

    bool isAcyclicLoopCopy = activePeeledLoop_ != nullptr && loopPeelingStage_ == LoopPeelingStage::PEELED_ITERATION;
    if (blockInfo->IsLoopHeader() && !isAcyclicLoopCopy) {
        BB *header = BlockAt(rpoIndex);
        ASSERT(header->PredecessorCount() == 1);
        CompileInfoFacts *incoming = GetOutgoingBlockState(header->GetPredecessor(0)).facts;
        ASSERT(incoming != nullptr);
        if (activePeeledLoop_ != nullptr && loopPeelingStage_ == LoopPeelingStage::OPTIMISTIC_LOOP) {
            ASSERT(peeledLoopEffects_ != nullptr);
            CompileInfoFactsAt(rpoIndex) = incoming->CloneForOptimisticLoopHeader(*peeledLoopEffects_);
        } else {
            CompileInfoFactsAt(rpoIndex) = incoming->CloneForLoopHeader();
        }
        return;
    }

    CompileInfoFacts *facts = nullptr;
    for (BB *predecessor : BlockAt(rpoIndex)->GetPredecessors()) {
        CompileInfoFacts *incoming = GetOutgoingBlockState(predecessor).facts;
        ASSERT(incoming != nullptr);
        if (facts == nullptr) {
            facts = incoming->Clone();
        } else {
            facts->Merge(*incoming);
        }
    }
    ASSERT(facts != nullptr);
    CompileInfoFactsAt(rpoIndex) = facts;
}

void GraphBuilder::InitCompileInfoFactsForCatchBlock(uint32_t rpoIndex)
{
    CatchBlockInputData *data = CatchBlockInputsAt(rpoIndex);
    ASSERT(data != nullptr);
    ASSERT(data->facts != nullptr);
    CompileInfoFactsAt(rpoIndex) = data->facts;
}

void GraphBuilder::WriteBackFrameStateToLoopHeader(SharedBCFrameState current, uint32_t rpoIndex)
{
    const BasicBlockInfo *blockInfo = preproc_->GetBasicBlockByRPO(rpoIndex);
    ASSERT(blockInfo->IsJump());
    ASSERT(blockInfo->jumpBlock->jumpPredecessors.size() == 2);  // 2 : One is entry, the other is loop-back

    uint32_t headerIndex = blockInfo->jumpBlock->rpoIndex;
    for (PhiVertex *phi : BlockAt(headerIndex)->GetPhis()) {
        ASSERT(phi->GetInputCount() == 2);  // 2 : One is entry, the other is loop-back
        ValueVertex *fromCurrent = current.Get(phi->GetOwner().GetId());
        phi->SetInput(1, fromCurrent != nullptr ? fromCurrent : undefinedValue_);
    }
}

void GraphBuilder::MergeFrameState(SharedBCFrameState dest, uint32_t rpoIndex,
                                   const OutgoingBlockState &incoming,
                                   uint32_t actualPredIndex, uint32_t actualNumPreds)
{
    const kungfu::BitSet &liveIn = analysis_->GetLiveInOfBlock(rpoIndex);
    incoming.frameState.ForEach([&, this](ValueVertex *fromPred, VRegIDType vregIndex) {
        if (!liveIn.TestBit(vregIndex)) {
            return;
        }
        ValueVertex *cur = dest.Get(vregIndex);
        if (cur == fromPred) {
            return;
        }
        if (cur == nullptr) {
            dest.Set(vregIndex, fromPred);
            return;
        }
        PhiVertex *phi = cur->TryCast<PhiVertex>();
        if (phi != nullptr && cur->GetOwner() == BlockAt(rpoIndex)) {
            phi->SetInput(actualPredIndex, fromPred);
            return;
        }
        phi = NewPhiVertex(BlockAt(rpoIndex), actualNumPreds, vregIndex);
        for (uint32_t k = 0; k < actualPredIndex; k++) {
            phi->SetInput(k, cur);
        }
        phi->SetInput(actualPredIndex, fromPred);
        dest.Set(vregIndex, phi);
    });
}

PhiVertex *GraphBuilder::NewPhiVertex(BB *owner, uint32_t numPredecessors, VRegIDType vreg)
{
    PhiVertex *phi = PhiVertex::New(chunk_, numPredecessors, VirtualRegister(vreg));
    phi->SetOwner(owner);
    owner->AddPhiVertex(phi);
    return phi;
}

template <class InputRange>
PhiVertex *GraphBuilder::NewPhiVertexWith(BB *owner, const InputRange &inputs, VRegIDType vreg)
{
    uint32_t numInputs = static_cast<uint32_t>(std::size(inputs));
    PhiVertex *phi = NewPhiVertex(owner, numInputs, vreg);

    auto iter = std::begin(inputs);
    for (uint32_t i = 0; i < numInputs; i++) {
        phi->SetInput(i, *iter);
        ++iter;
    }
    return phi;
}

// Populate Vertex bytecode offset: mixin-provided value takes priority; vertices without a mixin
// offset (no eager/lazy deopt mixin, or lazy mixin without frame state) fall back to the offset
// of the bytecode currently being processed. This keeps the SourcePositionTable anchor density
// at vertex level instead of deopt-point level.
template <class VertexT>
void GraphBuilder::PopulateVertexBytecodeOffset(VertexT *vertex)
{
    if constexpr (std::is_base_of_v<EagerDeoptimizableMixin, VertexT>) {
        vertex->SetBytecodeOffset(
            static_cast<const EagerDeoptimizableMixin *>(vertex)->GetBytecodeOffset());
        return;
    }
    if constexpr (std::is_base_of_v<LazyDeoptimizableMixin, VertexT>) {
        const auto *lazyMixin = static_cast<const LazyDeoptimizableMixin *>(vertex);
        if (lazyMixin->HasLazyDeoptFrameState()) {
            vertex->SetBytecodeOffset(lazyMixin->GetBytecodeOffset());
            return;
        }
    }
    vertex->SetBytecodeOffset(currentBytecodeOffset_);
}

template <class VertexT, class InputRange, class... Args>
VertexT *GraphBuilder::NewVertexImpl(BB *owner, const InputRange &inputs, Args &&...args)
{
    VertexT *vertex = Vertex::New<VertexT>(chunk_, inputs, std::forward<Args>(args)...);
    vertex->SetOwner(owner);
    owner->AddVertex(vertex);
    // At most one of: eager_deopt, lazy_deopt
    static_assert(CanEagerDeopt(VertexT::PROPERTIES) + CanLazyDeopt(VertexT::PROPERTIES) <= 1);

    PopulateVertexBytecodeOffset(vertex);

    return vertex;
}

template <class VertexT, class InputRange, class... Args>
VertexT *GraphBuilder::NewVertex(BB *owner, const InputRange &inputs, Args &&...args)
{
    VertexT *vertex = NewVertexImpl<VertexT>(owner, inputs, std::forward<Args>(args)...);
    if constexpr (CanWrite(VertexT::PROPERTIES)) {
        if (LoopEffects *effects = CurrentLoopEffects()) {
            SideEffectDescriptor effect = ArkSteedSideEffectClassifier::Classify(vertex);
            // Without facts there is no alias analysis for environment stores.
            effects->Record(effect.kind == SideEffectKind::ENV_SLOT_WRITE
                ? SideEffectDescriptor {SideEffectKind::UNKNOWN_CALL} : effect);
        }
    }
    return vertex;
}

template <class VertexT, class InputRange, class... Args>
VertexT *GraphBuilder::NewVertex(
    CompileInfoFacts *compileInfoFacts, BB *owner, const InputRange &inputs, Args &&...args)
{
    if constexpr (CseCanUseAvailableExpression<VertexT>()) {
        static_assert(!CanWrite(VertexT::PROPERTIES));
        CompileInfoFacts::ExpressionInputs expressionInputs(chunk_);
        for (ValueVertex *input : inputs) {
            expressionInputs.push_back(input);
        }

        CompileInfoFacts::ExpressionOptions options(chunk_);
        CseBuildExpressionOptions(options, args...);
        uint32_t hash = CseHashExpression(OpcodeOf<VertexT>, expressionInputs, options);
        // Read operations are versioned by the effect epoch.
        bool needsEpochCheck = CanRead(VertexT::PROPERTIES);
        ValueVertex *cached =
            compileInfoFacts->FindExpression(hash, OpcodeOf<VertexT>, expressionInputs, options, needsEpochCheck);
        // The cached vertex is reusable if one of the following conditions is true:
        // 1. The cached vertex is not a read vertex;
        // 2. For read vertices, the cached vertex is created in the current block.
        // Note: Constraint 2 can be relaxed in the future such that the cached vertex dominates current block.
        if (cached != nullptr && (!needsEpochCheck || cached->GetOwner() == owner)) {
            return cached->Cast<VertexT>();
        }

        VertexT *vertex = NewVertexImpl<VertexT>(owner, inputs, std::forward<Args>(args)...);
        compileInfoFacts->AddExpression(hash, vertex, expressionInputs, options, needsEpochCheck);
        return vertex;
    }
    VertexT *vertex = NewVertexImpl<VertexT>(owner, inputs, std::forward<Args>(args)...);
    if constexpr (CanWrite(VertexT::PROPERTIES)) {
        SideEffectDescriptor effect = ArkSteedSideEffectClassifier::Classify(vertex);
        compileInfoFacts->MarkPossibleSideEffect(effect, CurrentLoopEffects());
    }
    return vertex;
}

LoopEffects *GraphBuilder::CurrentLoopEffects()
{
    return loopEffectsStack_.empty() ? nullptr : loopEffectsStack_.back().effects;
}

JumpVertex *GraphBuilder::FinishBlockWithJump(BB *owner, BB *target)
{
    auto *jumpVertex = FinishBlockWith<JumpVertex>(owner, {}, target);
    jumpVertex->SetPredecessorId(target->PredecessorCount());
    target->AddPredecessor(owner);
    return jumpVertex;
}

JumpLoopVertex *GraphBuilder::FinishBlockWithJumpLoop(BB *owner, BB *target)
{
    auto *jumpLoopVertex = FinishBlockWith<JumpLoopVertex>(owner, {}, chunk_, target);
    jumpLoopVertex->SetPredecessorId(target->PredecessorCount());
    target->AddPredecessor(owner);
    return jumpLoopVertex;
}

ControlVertex *GraphBuilder::FinishBlockWithBranch(BB *owner, ValueVertex *input, BB *targetIfTrue, BB *targetIfFalse)
{
    auto finishWithTargets = [targetIfTrue, targetIfFalse, owner](ControlVertex *vertex) {
        targetIfTrue->AddPredecessor(owner);
        targetIfFalse->AddPredecessor(owner);
        return vertex;
    };

    if (auto *compare = input->TryCast<I32ConditionCheckVertex>()) {
        return finishWithTargets(
            FinishBlockWith<BranchIfInt32CompareVertex>(owner,
                                                        {compare->GetInput(I32ConditionCheckVertex::LEFT_INDEX),
                                                         compare->GetInput(I32ConditionCheckVertex::RIGHT_INDEX)},
                                                        targetIfTrue, targetIfFalse, compare->GetCondition()));
    }
    if (auto *compare = input->TryCast<F64ConditionCheckVertex>()) {
        return finishWithTargets(
            FinishBlockWith<BranchIfFloat64CompareVertex>(owner,
                                                          {compare->GetInput(F64ConditionCheckVertex::LEFT_INDEX),
                                                           compare->GetInput(F64ConditionCheckVertex::RIGHT_INDEX)},
                                                          targetIfTrue, targetIfFalse, compare->GetCondition()));
    }
    if (auto *equal = input->TryCast<TaggedEqualVertex>()) {
        return finishWithTargets(FinishBlockWith<BranchIfReferenceEqualVertex>(
            owner, {equal->GetInput(TaggedEqualVertex::LEFT_INDEX), equal->GetInput(TaggedEqualVertex::RIGHT_INDEX)},
            targetIfTrue, targetIfFalse));
    }
    if (auto *notEqual = input->TryCast<TaggedNotEqualVertex>()) {
        return finishWithTargets(
            FinishBlockWith<BranchIfReferenceEqualVertex>(owner,
                                                          {notEqual->GetInput(TaggedNotEqualVertex::LEFT_INDEX),
                                                           notEqual->GetInput(TaggedNotEqualVertex::RIGHT_INDEX)},
                                                          targetIfFalse, targetIfTrue));
    }

    auto *branchVertex = FinishBlockWith<BranchIfTrueVertex>(owner, {input}, targetIfTrue, targetIfFalse);
    return finishWithTargets(branchVertex);
}

template <class BranchVertexT, class InputRange, class... Args>
BranchVertexT *GraphBuilder::FinishBlockWithBranch(BB *owner, const InputRange &inputs, BB *targetIfTrue,
                                                   BB *targetIfFalse, Args &&...args)
{
    auto *vertex =
        FinishBlockWith<BranchVertexT>(owner, inputs, targetIfTrue, targetIfFalse, std::forward<Args>(args)...);
    targetIfTrue->AddPredecessor(owner);
    targetIfFalse->AddPredecessor(owner);
    return vertex;
}

template <class VertexT, class InputRange, class... Args>
VertexT *GraphBuilder::FinishBlockWith(BB *owner, const InputRange &inputs, Args &&...args)
{
    VertexT *vertex = Vertex::New<VertexT>(chunk_, inputs, std::forward<Args>(args)...);
    vertex->SetOwner(owner);
    owner->SetControlVertex(vertex);
    graph_->Add(owner);
    // Control vertices cannot have lazy deopt, throw, or write side effects
    // Note: ThrowVertex is a special case that can throw
    static_assert(!CanLazyDeopt(VertexT::PROPERTIES) && !CanWrite(VertexT::PROPERTIES));

    PopulateVertexBytecodeOffset(vertex);

    return vertex;
}

BB *GraphBuilder::NewBlock()
{
    BB *result = BB::New(chunk_);
    // TODO: For legacy code only. To be removed.
    result->SetRegisterMergeState(chunk_->New<RegisterMergeState>());
    return result;
}

BB *GraphBuilder::ActivateNonCatchBlock(uint32_t rpoIndex)
{
    BB *&block = BlockAt(rpoIndex);
    if (block == nullptr) {
        block = NewBlock();
    }
    if (activePeeledLoop_ != nullptr && !activePeeledLoop_->Contains(rpoIndex)) {
        ASSERT(currentSourceRpoIndex_ != BytecodePreprocessor::NULL_INDEX);
        return GetOrCreateLoopExitLanding(currentSourceRpoIndex_, rpoIndex, block);
    }
    return block;
}

BB *GraphBuilder::ActivateCatchBlock(GraphBuilder::CatchBlockInputData **inputData, uint32_t rpoIndex)
{
    BB *&block = BlockAt(rpoIndex);
    CatchBlockInputData *&data = CatchBlockInputsAt(rpoIndex);
    if (UNLIKELY(block == nullptr)) {
        block = NewBlock();
        data = chunk_->New<CatchBlockInputData>(analysis_->GetLiveInOfBlock(rpoIndex), chunk_);
    }
    ASSERT(data != nullptr);
    *inputData = data;
    return block;
}

LoadTaggedFieldVertex *GraphBuilder::ActivateGlobalEnv()
{
    if (UNLIKELY(lazyGlobalEnv_ == nullptr)) {
        int32_t globalEnvOffset = static_cast<int32_t>(GlobalEnv::HEADER_SIZE);
        lazyGlobalEnv_ = NewVertex<LoadTaggedFieldVertex>(blocks_[0], {initialLexicalEnv_}, globalEnvOffset);
    }
    return lazyGlobalEnv_;
}

constexpr uintptr_t NO_CATCH_BLOCK_TAG = 1;

struct GraphBuilder::BytecodeVisitor {
    struct NamedLoadAccessInfo {
        JSHClass *receiverHClass {nullptr};
        JSHClass *holderHClass {nullptr};
        ArkSteedObjectRef holderRef {};
        uint32_t holderHandleIndex {JitCompilationEnv::INVALID_HEAP_CONSTANT_INDEX};
        std::vector<JSHClass *> lookupStartObjectHClasses;
        std::vector<ArkSteedHClassRef> lookupStartObjectHClassRefs;
        std::vector<JSHClass *> expectedPrototypeHClasses;
        std::vector<ArkSteedHClassRef> expectedPrototypeHClassRefs;
        PropertyLookupResult plr;
        uint32_t holderDepth {0};
        bool isConst {false};
        bool isJSArrayLength {false};
        JSType typedArrayLengthType {JSType::INVALID};
        OnHeapMode typedArrayOnHeapMode {OnHeapMode::NONE};
        bool canAssumeStableHClasses {false};
        bool hasStableProtoChain {false};
    };

    using NamedLoadAccessInfoOpt = std::optional<NamedLoadAccessInfo>;
    using NamedLoadAccessInfosOpt = std::optional<std::vector<NamedLoadAccessInfo>>;

    bool Visit(const BytecodeInfo *bcInfo, uint32_t bcIndex)
    {
        if (currentBlock->GetControlVertex() != nullptr) {
            return false;
        }
        currentBcInfo = bcInfo;
        currentBcIndex = bcIndex;
        self->currentBytecodeOffset_ = self->preproc_->GetBytecodeOffset(bcIndex);
        if (self->GetOptions()->GetCompilerArkSteedDeoptOnInsufficientProfile() && bcInfo->IsInsufficientProfile()) {
            EmitUnconditionalDeopt();
            return false;
        }

        switch (bcInfo->GetOpcode()) {
            case kungfu::EcmaOpcode::NOP:  // Nop: Nothing to do
                break;
            // -------- Category #1: Register Moves --------
            case kungfu::EcmaOpcode::MOV_V4_V4:
            case kungfu::EcmaOpcode::MOV_V8_V8:
            case kungfu::EcmaOpcode::MOV_V16_V16:
                frameState.Set(bcInfo->vregOut, LoadRegister(bcInfo, 0));
                break;
            case kungfu::EcmaOpcode::STA_V8:
                frameState.Set(bcInfo->vregOut, frameState.GetAcc());
                break;
            case kungfu::EcmaOpcode::LDA_V8:
                frameState.SetAcc(LoadRegister(bcInfo, 0));
                break;
            case kungfu::EcmaOpcode::LDFUNCTION:
                frameState.SetAcc(LoadParam(CALL_TARGET_PARAM_INDEX));
                break;
            case kungfu::EcmaOpcode::LDNEWTARGET:
                frameState.SetAcc(LoadParam(NEW_TARGET_PARAM_INDEX));
                break;
            case kungfu::EcmaOpcode::LDTHIS:
                frameState.SetAcc(LoadParam(THIS_OBJECT_PARAM_INDEX));
                break;
            // -------- Category #2: Constant Loads --------
            case kungfu::EcmaOpcode::LDNAN:
                LowerLdTaggedConstant(base::NumberHelper::GetNaN());
                break;
            case kungfu::EcmaOpcode::LDINFINITY:
                LowerLdTaggedConstant(base::NumberHelper::GetPositiveInfinity());
                break;
            case kungfu::EcmaOpcode::LDUNDEFINED:
                LowerLdTaggedConstant(JSTaggedValue::VALUE_UNDEFINED);
                break;
            case kungfu::EcmaOpcode::LDNULL:
                LowerLdTaggedConstant(JSTaggedValue::VALUE_NULL);
                break;
            case kungfu::EcmaOpcode::LDTRUE:
                LowerLdTaggedConstant(JSTaggedValue::VALUE_TRUE);
                break;
            case kungfu::EcmaOpcode::LDFALSE:
                LowerLdTaggedConstant(JSTaggedValue::VALUE_FALSE);
                break;
            case kungfu::EcmaOpcode::LDHOLE:
                LowerLdTaggedConstant(JSTaggedValue::VALUE_HOLE);
                break;
            case kungfu::EcmaOpcode::LDAI_IMM32:
                LowerLdaiImm32(bcInfo);
                break;
            case kungfu::EcmaOpcode::FLDAI_IMM64:
                LowerFldaiImm64(bcInfo);
                break;
            case kungfu::EcmaOpcode::LDBIGINT_ID16:
                LowerLdBigInt(bcInfo);
                break;
            case kungfu::EcmaOpcode::LDA_STR_ID16:
                LowerLdString(bcInfo);
                break;
            // -------- Category #3: Unary Arithmetic --------
            case kungfu::EcmaOpcode::INC_IMM8:
                LowerInc();
                break;
            case kungfu::EcmaOpcode::DEC_IMM8:
                LowerDec();
                break;
            case kungfu::EcmaOpcode::NEG_IMM8:
                LowerNeg();
                break;
            case kungfu::EcmaOpcode::NOT_IMM8:
                LowerNot();
                break;
            // -------- Category #4: Binary Arithmetic --------
            case kungfu::EcmaOpcode::ADD2_IMM8_V8:
                LowerAdd2(bcInfo);
                break;
            case kungfu::EcmaOpcode::SUB2_IMM8_V8:
                LowerSub2(bcInfo);
                break;
            case kungfu::EcmaOpcode::MUL2_IMM8_V8:
                LowerMul2(bcInfo);
                break;
            case kungfu::EcmaOpcode::DIV2_IMM8_V8:
                LowerDiv2(bcInfo);
                break;
            case kungfu::EcmaOpcode::MOD2_IMM8_V8:
                LowerMod2(bcInfo);
                break;
            case kungfu::EcmaOpcode::EXP_IMM8_V8:
                LowerExp(bcInfo);
                break;
            case kungfu::EcmaOpcode::SHL2_IMM8_V8:
                LowerShl2(bcInfo);
                break;
            case kungfu::EcmaOpcode::SHR2_IMM8_V8:
                LowerShr2(bcInfo);
                break;
            case kungfu::EcmaOpcode::ASHR2_IMM8_V8:
                LowerAshr2(bcInfo);
                break;
            case kungfu::EcmaOpcode::AND2_IMM8_V8:
                LowerAnd2(bcInfo);
                break;
            case kungfu::EcmaOpcode::OR2_IMM8_V8:
                LowerOr2(bcInfo);
                break;
            case kungfu::EcmaOpcode::XOR2_IMM8_V8:
                LowerXor2(bcInfo);
                break;
            // -------- Category #5: Comparisons --------
            case kungfu::EcmaOpcode::EQ_IMM8_V8:
                LowerEq(bcInfo);
                break;
            case kungfu::EcmaOpcode::NOTEQ_IMM8_V8:
                LowerNotEq(bcInfo);
                break;
            case kungfu::EcmaOpcode::LESS_IMM8_V8:
                LowerLess(bcInfo);
                break;
            case kungfu::EcmaOpcode::LESSEQ_IMM8_V8:
                LowerLessEq(bcInfo);
                break;
            case kungfu::EcmaOpcode::GREATER_IMM8_V8:
                LowerGreater(bcInfo);
                break;
            case kungfu::EcmaOpcode::GREATEREQ_IMM8_V8:
                LowerGreaterEq(bcInfo);
                break;
            case kungfu::EcmaOpcode::STRICTNOTEQ_IMM8_V8:
                LowerStrictNotEq(bcInfo);
                break;
            case kungfu::EcmaOpcode::STRICTEQ_IMM8_V8:
                LowerStrictEq(bcInfo);
                break;
            case kungfu::EcmaOpcode::ISTRUE:
            case kungfu::EcmaOpcode::CALLRUNTIME_ISTRUE_PREF_IMM8:
                LowerIsTrue();
                break;
            case kungfu::EcmaOpcode::ISFALSE:
            case kungfu::EcmaOpcode::CALLRUNTIME_ISFALSE_PREF_IMM8:
                LowerIsFalse();
                break;
            // -------- Category #6: Type Conversions --------
            case kungfu::EcmaOpcode::TONUMBER_IMM8:
                LowerToNumber();
                break;
            case kungfu::EcmaOpcode::TONUMERIC_IMM8:
                LowerToNumeric();
                break;
            case kungfu::EcmaOpcode::CALLRUNTIME_TOPROPERTYKEY_PREF_NONE:
                LowerToPropertyKey();
                break;
            // -------- Category #7: Property Access --------
            case kungfu::EcmaOpcode::TRYLDGLOBALBYNAME_IMM8_ID16:
            case kungfu::EcmaOpcode::TRYLDGLOBALBYNAME_IMM16_ID16:
                LowerTryLdGlobalByName(bcInfo, bcIndex);
                break;
            case kungfu::EcmaOpcode::LDGLOBALVAR_IMM16_ID16:
                LowerLdGlobalVar(bcInfo, bcIndex);
                break;
            case kungfu::EcmaOpcode::STGLOBALVAR_IMM16_ID16:
                LowerStGlobalVar(bcInfo, bcIndex);
                break;
            case kungfu::EcmaOpcode::LDSYMBOL:
                LowerLdSymbol();
                break;
            case kungfu::EcmaOpcode::LDGLOBAL:
                LowerLdGlobal();
                break;
            case kungfu::EcmaOpcode::LDOBJBYNAME_IMM8_ID16:
            case kungfu::EcmaOpcode::LDOBJBYNAME_IMM16_ID16:
                LowerLdObjByName(bcInfo, bcIndex);
                break;
            case kungfu::EcmaOpcode::STOBJBYNAME_IMM8_ID16_V8:
            case kungfu::EcmaOpcode::STOBJBYNAME_IMM16_ID16_V8:
                LowerStObjByName(bcInfo, bcIndex);
                break;
            case kungfu::EcmaOpcode::LDOBJBYINDEX_IMM8_IMM16:
            case kungfu::EcmaOpcode::LDOBJBYINDEX_IMM16_IMM16:
            case kungfu::EcmaOpcode::WIDE_LDOBJBYINDEX_PREF_IMM32:
                LowerLdObjByIndex(bcInfo);
                break;
            case kungfu::EcmaOpcode::STOBJBYINDEX_IMM8_V8_IMM16:
            case kungfu::EcmaOpcode::STOBJBYINDEX_IMM16_V8_IMM16:
            case kungfu::EcmaOpcode::WIDE_STOBJBYINDEX_PREF_V8_IMM32:
                LowerStObjByIndex(bcInfo);
                break;
            case kungfu::EcmaOpcode::LDOBJBYVALUE_IMM8_V8:
            case kungfu::EcmaOpcode::LDOBJBYVALUE_IMM16_V8:
                LowerLdObjByValue(bcInfo, bcIndex);
                break;
            case kungfu::EcmaOpcode::STOBJBYVALUE_IMM8_V8_V8:
            case kungfu::EcmaOpcode::STOBJBYVALUE_IMM16_V8_V8:
                LowerStObjByValue(bcInfo, bcIndex);
                break;
            case kungfu::EcmaOpcode::STOWNBYVALUE_IMM8_V8_V8:
            case kungfu::EcmaOpcode::STOWNBYVALUE_IMM16_V8_V8:
                LowerStOwnByValue(bcInfo);
                break;
            case kungfu::EcmaOpcode::STOWNBYINDEX_IMM8_V8_IMM16:
            case kungfu::EcmaOpcode::STOWNBYINDEX_IMM16_V8_IMM16:
                LowerStOwnByIndex(bcInfo, bcIndex);
                break;
            case kungfu::EcmaOpcode::WIDE_STOWNBYINDEX_PREF_V8_IMM32:
                LowerWideStOwnByIndex(bcInfo);
                break;
            case kungfu::EcmaOpcode::STOWNBYNAME_IMM8_ID16_V8:
            case kungfu::EcmaOpcode::STOWNBYNAME_IMM16_ID16_V8:
                LowerStOwnByName(bcInfo);
                break;
            case kungfu::EcmaOpcode::LDTHISBYVALUE_IMM8:
            case kungfu::EcmaOpcode::LDTHISBYVALUE_IMM16:
                LowerLdThisByValue(bcInfo, bcIndex);
                break;
            case kungfu::EcmaOpcode::STTHISBYVALUE_IMM8_V8:
            case kungfu::EcmaOpcode::STTHISBYVALUE_IMM16_V8:
                LowerStThisByValue(bcInfo, bcIndex);
                break;
            case kungfu::EcmaOpcode::LDTHISBYNAME_IMM8_ID16:
            case kungfu::EcmaOpcode::LDTHISBYNAME_IMM16_ID16:
                LowerLdThisByName(bcInfo);
                break;
            case kungfu::EcmaOpcode::STTHISBYNAME_IMM8_ID16:
            case kungfu::EcmaOpcode::STTHISBYNAME_IMM16_ID16:
                LowerStThisByName(bcInfo);
                break;
            case kungfu::EcmaOpcode::LDSUPERBYVALUE_IMM8_V8:
            case kungfu::EcmaOpcode::LDSUPERBYVALUE_IMM16_V8:
                LowerLdSuperByValue(bcInfo);
                break;
            case kungfu::EcmaOpcode::STSUPERBYVALUE_IMM8_V8_V8:
            case kungfu::EcmaOpcode::STSUPERBYVALUE_IMM16_V8_V8:
                LowerStSuperByValue(bcInfo);
                break;
            case kungfu::EcmaOpcode::LDSUPERBYNAME_IMM8_ID16:
            case kungfu::EcmaOpcode::LDSUPERBYNAME_IMM16_ID16:
                LowerLdSuperByName(bcInfo);
                break;
            case kungfu::EcmaOpcode::STSUPERBYNAME_IMM8_ID16_V8:
            case kungfu::EcmaOpcode::STSUPERBYNAME_IMM16_ID16_V8:
                LowerStSuperByName(bcInfo);
                break;
            case kungfu::EcmaOpcode::TRYSTGLOBALBYNAME_IMM8_ID16:
            case kungfu::EcmaOpcode::TRYSTGLOBALBYNAME_IMM16_ID16:
                LowerTryStGlobalByName(bcInfo, bcIndex);
                break;
            case kungfu::EcmaOpcode::STCONSTTOGLOBALRECORD_IMM16_ID16:
                LowerStConstToGlobalRecord(bcInfo, true);
                break;
            case kungfu::EcmaOpcode::STTOGLOBALRECORD_IMM16_ID16:
                LowerStConstToGlobalRecord(bcInfo, false);
                break;
            case kungfu::EcmaOpcode::STOWNBYVALUEWITHNAMESET_IMM8_V8_V8:
            case kungfu::EcmaOpcode::STOWNBYVALUEWITHNAMESET_IMM16_V8_V8:
                LowerStOwnByValueWithNameSet(bcInfo);
                break;
            case kungfu::EcmaOpcode::STOWNBYNAMEWITHNAMESET_IMM8_ID16_V8:
            case kungfu::EcmaOpcode::STOWNBYNAMEWITHNAMESET_IMM16_ID16_V8:
                LowerStOwnByNameWithNameSet(bcInfo);
                break;
            case kungfu::EcmaOpcode::LDPRIVATEPROPERTY_IMM8_IMM16_IMM16:
                LowerLdPrivateProperty(bcInfo);
                break;
            case kungfu::EcmaOpcode::STPRIVATEPROPERTY_IMM8_IMM16_IMM16_V8:
                LowerStPrivateProperty(bcInfo);
                break;
            // -------- Category #8: Function Calls --------
            case kungfu::EcmaOpcode::CALLARG0_IMM8:
                LowerCallArg0(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLARG1_IMM8_V8:
                LowerCallArg1(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLARGS2_IMM8_V8_V8:
                LowerCallArgs2(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLARGS3_IMM8_V8_V8_V8:
                LowerCallArgs3(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLRUNTIME_CALLINIT_PREF_IMM8_V8:
                LowerCallInit(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLTHIS0_IMM8_V8:
                LowerCallThis0(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLTHIS1_IMM8_V8_V8:
                LowerCallThis1(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLTHIS2_IMM8_V8_V8_V8:
                LowerCallThis2(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLTHIS3_IMM8_V8_V8_V8_V8:
                LowerCallThis3(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLRANGE_IMM8_IMM8_V8:
            case kungfu::EcmaOpcode::WIDE_CALLRANGE_PREF_IMM16_V8:
                LowerCallRange(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLTHISRANGE_IMM8_IMM8_V8:
            case kungfu::EcmaOpcode::WIDE_CALLTHISRANGE_PREF_IMM16_V8:
                LowerCallThisRange(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLTHIS0WITHNAME_IMM8_ID16_V8:
            case kungfu::EcmaOpcode::CALLTHIS1WITHNAME_IMM8_ID16_V8_V8:
            case kungfu::EcmaOpcode::CALLTHIS2WITHNAME_IMM8_ID16_V8_V8_V8:
            case kungfu::EcmaOpcode::CALLTHIS3WITHNAME_IMM8_ID16_V8_V8_V8_V8:
            case kungfu::EcmaOpcode::CALLTHISRANGEWITHNAME_IMM8_IMM8_ID16_V8:
            case kungfu::EcmaOpcode::WIDE_CALLTHISRANGEWITHNAME_PREF_IMM16_ID16_V8:
                LowerCallThisWithName(bcInfo);
                break;
            case kungfu::EcmaOpcode::APPLY_IMM8_V8_V8:
                LowerCallSpread(bcInfo);
                break;
            case kungfu::EcmaOpcode::SUPERCALLTHISRANGE_IMM8_IMM8_V8:
            case kungfu::EcmaOpcode::WIDE_SUPERCALLTHISRANGE_PREF_IMM16_V8:
                LowerSuperCallThisRange(bcInfo);
                break;
            case kungfu::EcmaOpcode::SUPERCALLARROWRANGE_IMM8_IMM8_V8:
            case kungfu::EcmaOpcode::WIDE_SUPERCALLARROWRANGE_PREF_IMM16_V8:
                LowerSuperCallArrowRange(bcInfo);
                break;
            case kungfu::EcmaOpcode::SUPERCALLSPREAD_IMM8_V8:
                LowerSuperCallSpread(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLRUNTIME_SUPERCALLFORWARDALLARGS_PREF_V8:
                LowerSuperCallForwardAllArgs(bcInfo);
                break;
            case kungfu::EcmaOpcode::NEWOBJAPPLY_IMM8_V8:
            case kungfu::EcmaOpcode::NEWOBJAPPLY_IMM16_V8:
                LowerNewObjApply(bcInfo);
                break;
            case kungfu::EcmaOpcode::NEWOBJRANGE_IMM8_IMM8_V8:
            case kungfu::EcmaOpcode::NEWOBJRANGE_IMM16_IMM8_V8:
            case kungfu::EcmaOpcode::WIDE_NEWOBJRANGE_PREF_IMM16_V8:
                LowerNewObjRange(bcInfo);
                break;
            // -------- Category #9: Object/Array Creation --------
            case kungfu::EcmaOpcode::CREATEITERRESULTOBJ_V8_V8:
                LowerCreateIterResultObj(bcInfo);
                break;
            case kungfu::EcmaOpcode::CREATEEMPTYARRAY_IMM8:
            case kungfu::EcmaOpcode::CREATEEMPTYARRAY_IMM16:
                LowerCreateEmptyArray(bcInfo);
                break;
            case kungfu::EcmaOpcode::CREATEEMPTYOBJECT:
                LowerCreateEmptyObject();
                break;
            case kungfu::EcmaOpcode::CREATEOBJECTWITHBUFFER_IMM8_ID16:
            case kungfu::EcmaOpcode::CREATEOBJECTWITHBUFFER_IMM16_ID16:
                LowerCreateObjectWithBuffer(bcInfo);
                break;
            case kungfu::EcmaOpcode::CREATEOBJECTWITHEXCLUDEDKEYS_IMM8_V8_V8:
            case kungfu::EcmaOpcode::WIDE_CREATEOBJECTWITHEXCLUDEDKEYS_PREF_IMM16_V8_V8:
                LowerCreateObjectWithExcludedKeys(bcInfo);
                break;
            case kungfu::EcmaOpcode::CREATEARRAYWITHBUFFER_IMM8_ID16:
            case kungfu::EcmaOpcode::CREATEARRAYWITHBUFFER_IMM16_ID16:
                LowerCreateArrayWithBuffer(bcInfo);
                break;
            case kungfu::EcmaOpcode::CREATEREGEXPWITHLITERAL_IMM8_ID16_IMM8:
            case kungfu::EcmaOpcode::CREATEREGEXPWITHLITERAL_IMM16_ID16_IMM8:
                LowerCreateRegExpWithLiteral(bcInfo);
                break;
            // -------- Category #10: Class/Function/Field Definition --------
            case kungfu::EcmaOpcode::DEFINEMETHOD_IMM8_ID16_IMM8:
            case kungfu::EcmaOpcode::DEFINEMETHOD_IMM16_ID16_IMM8:
                LowerDefineMethod(bcInfo);
                break;
            case kungfu::EcmaOpcode::DEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8:
                LowerDefineGetterSetterByValue(bcInfo);
                break;
            case kungfu::EcmaOpcode::DEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8:
            case kungfu::EcmaOpcode::DEFINECLASSWITHBUFFER_IMM16_ID16_ID16_IMM16_V8:
                LowerDefineClassWithBuffer(bcInfo);
                break;
            case kungfu::EcmaOpcode::DEFINEFUNC_IMM8_ID16_IMM8:
            case kungfu::EcmaOpcode::DEFINEFUNC_IMM16_ID16_IMM8:
                LowerDefineFunc(bcInfo);
                break;
            case kungfu::EcmaOpcode::DEFINEPROPERTYBYNAME_IMM8_ID16_V8:
                LowerDefinePropertyByName(bcInfo, bcIndex);
                break;
            case kungfu::EcmaOpcode::DEFINEFIELDBYNAME_IMM8_ID16_V8:
                LowerDefineFieldByName(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLRUNTIME_DEFINEFIELDBYVALUE_PREF_IMM8_V8_V8:
                LowerDefineFieldByValue(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLRUNTIME_DEFINEFIELDBYINDEX_PREF_IMM8_IMM32_V8:
                LowerDefineFieldByIndex(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLRUNTIME_CREATEPRIVATEPROPERTY_PREF_IMM16_ID16:
                LowerCreatePrivateProperty(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLRUNTIME_DEFINEPRIVATEPROPERTY_PREF_IMM8_IMM16_IMM16_V8:
                LowerDefinePrivateProperty(bcInfo);
                break;
            // -------- Category #11: Iterators --------
            case kungfu::EcmaOpcode::GETPROPITERATOR:
                LowerGetPropIterator();
                break;
            case kungfu::EcmaOpcode::CLOSEITERATOR_IMM8_V8:
            case kungfu::EcmaOpcode::CLOSEITERATOR_IMM16_V8:
                LowerCloseIterator(bcInfo);
                break;
            case kungfu::EcmaOpcode::GETITERATOR_IMM8:
            case kungfu::EcmaOpcode::GETITERATOR_IMM16:
                LowerGetIterator();
                break;
            case kungfu::EcmaOpcode::GETNEXTPROPNAME_V8:
                LowerGetNextPropName(bcInfo);
                break;
            // -------- Category #12: Lexical Environment --------
            case kungfu::EcmaOpcode::NEWLEXENV_IMM8:
            case kungfu::EcmaOpcode::WIDE_NEWLEXENV_PREF_IMM16:
                LowerNewLexicalEnv(bcInfo);
                break;
            case kungfu::EcmaOpcode::NEWLEXENVWITHNAME_IMM8_ID16:
            case kungfu::EcmaOpcode::WIDE_NEWLEXENVWITHNAME_PREF_IMM16_ID16:
                LowerNewLexicalEnvWithName(bcInfo);
                break;
            case kungfu::EcmaOpcode::POPLEXENV:
                LowerPopLexicalEnv(bcInfo);
                break;
            case kungfu::EcmaOpcode::LDLEXVAR_IMM4_IMM4:
            case kungfu::EcmaOpcode::LDLEXVAR_IMM8_IMM8:
            case kungfu::EcmaOpcode::WIDE_LDLEXVAR_PREF_IMM16_IMM16:
                LowerLdLexVar(bcInfo);
                break;
            case kungfu::EcmaOpcode::STLEXVAR_IMM4_IMM4:
            case kungfu::EcmaOpcode::STLEXVAR_IMM8_IMM8:
            case kungfu::EcmaOpcode::WIDE_STLEXVAR_PREF_IMM16_IMM16:
                LowerStLexVar(bcInfo);
                break;
            // -------- Category #13: Modules --------
            case kungfu::EcmaOpcode::STMODULEVAR_IMM8:
            case kungfu::EcmaOpcode::WIDE_STMODULEVAR_PREF_IMM16:
                LowerStModuleVar(bcInfo);
                break;
            case kungfu::EcmaOpcode::DYNAMICIMPORT:
                LowerDynamicImport();
                break;
            case kungfu::EcmaOpcode::LDEXTERNALMODULEVAR_IMM8:
            case kungfu::EcmaOpcode::WIDE_LDEXTERNALMODULEVAR_PREF_IMM16:
                LowerLdExternalModuleVar(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLRUNTIME_LDLAZYMODULEVAR_PREF_IMM8:
            case kungfu::EcmaOpcode::CALLRUNTIME_WIDELDLAZYMODULEVAR_PREF_IMM16:
                LowerLdLazyExternalModuleVar(bcInfo);
                break;
            case kungfu::EcmaOpcode::GETMODULENAMESPACE_IMM8:
            case kungfu::EcmaOpcode::WIDE_GETMODULENAMESPACE_PREF_IMM16:
                LowerGetModuleNamespace(bcInfo);
                break;
            case kungfu::EcmaOpcode::WIDE_LDPATCHVAR_PREF_IMM16:
                LowerLdPatchVar(bcInfo);
                break;
            case kungfu::EcmaOpcode::WIDE_STPATCHVAR_PREF_IMM16:
                LowerStPatchVar(bcInfo);
                break;
            case kungfu::EcmaOpcode::LDLOCALMODULEVAR_IMM8:
            case kungfu::EcmaOpcode::WIDE_LDLOCALMODULEVAR_PREF_IMM16:
                LowerLdLocalModuleVar(bcInfo);
                break;
            // -------- Category #14: Miscellaneous --------
            case kungfu::EcmaOpcode::GETUNMAPPEDARGS:
                LowerGetUnmappedArgs();
                break;
            case kungfu::EcmaOpcode::TYPEOF_IMM8:
            case kungfu::EcmaOpcode::TYPEOF_IMM16:
                LowerTypeOf();
                break;
            case kungfu::EcmaOpcode::DELOBJPROP_V8:
                LowerDelObjProp(bcInfo);
                break;
            case kungfu::EcmaOpcode::ISIN_IMM8_V8:
                LowerIsIn(bcInfo);
                break;
            case kungfu::EcmaOpcode::INSTANCEOF_IMM8_V8:
                LowerInstanceOf(bcInfo);
                break;
            case kungfu::EcmaOpcode::GETTEMPLATEOBJECT_IMM8:
            case kungfu::EcmaOpcode::GETTEMPLATEOBJECT_IMM16:
                LowerGetTemplateObject();
                break;
            case kungfu::EcmaOpcode::SETOBJECTWITHPROTO_IMM8_V8:
            case kungfu::EcmaOpcode::SETOBJECTWITHPROTO_IMM16_V8:
                LowerSetObjectWithProto(bcInfo);
                break;
            case kungfu::EcmaOpcode::COPYDATAPROPERTIES_V8:
                LowerCopyDataProperties(bcInfo);
                break;
            case kungfu::EcmaOpcode::STARRAYSPREAD_V8_V8:
                LowerStoreArraySpread(bcInfo);
                break;
            case kungfu::EcmaOpcode::COPYRESTARGS_IMM8:
            case kungfu::EcmaOpcode::WIDE_COPYRESTARGS_PREF_IMM16:
                LowerCopyRestArgs(bcInfo);
                break;
            case kungfu::EcmaOpcode::TESTIN_IMM8_IMM16_IMM16:
                LowerTestIn(bcInfo);
                break;
            case kungfu::EcmaOpcode::CALLRUNTIME_NOTIFYCONCURRENTRESULT_PREF_NONE:
                LowerNotifyConcurrentResult();
                break;
            // -------- Category #15: Exceptions --------
            case kungfu::EcmaOpcode::THROW_PREF_NONE:
                LowerThrow();
                break;
            case kungfu::EcmaOpcode::THROW_CONSTASSIGNMENT_PREF_V8:
                LowerThrowConstAssignment(bcInfo);
                break;
            case kungfu::EcmaOpcode::THROW_NOTEXISTS_PREF_NONE:
                LowerThrowNotExists();
                break;
            case kungfu::EcmaOpcode::THROW_PATTERNNONCOERCIBLE_PREF_NONE:
                LowerThrowPatternNonCoercible();
                break;
            case kungfu::EcmaOpcode::THROW_DELETESUPERPROPERTY_PREF_NONE:
                LowerThrowDeleteSuperProperty();
                break;
            case kungfu::EcmaOpcode::THROW_IFNOTOBJECT_PREF_V8:
                if (LowerThrowIfNotObject(bcInfo) == LoweringResult::BLOCK_TERMINATED) {
                    return false;
                }
                break;
            case kungfu::EcmaOpcode::THROW_UNDEFINEDIFHOLE_PREF_V8_V8:
                if (LowerThrowUndefinedIfHole(bcInfo) == LoweringResult::BLOCK_TERMINATED) {
                    return false;
                }
                break;
            case kungfu::EcmaOpcode::THROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16:
                if (LowerThrowUndefinedIfHoleWithName(bcInfo) == LoweringResult::BLOCK_TERMINATED) {
                    return false;
                }
                break;
            case kungfu::EcmaOpcode::THROW_IFSUPERNOTCORRECTCALL_PREF_IMM8:
            case kungfu::EcmaOpcode::THROW_IFSUPERNOTCORRECTCALL_PREF_IMM16:
                LowerThrowIfSuperNotCorrectCall(bcInfo);
                break;
            // -------- Category #16: Control Flow --------
            case kungfu::EcmaOpcode::JEQZ_IMM8:
            case kungfu::EcmaOpcode::JEQZ_IMM16:
            case kungfu::EcmaOpcode::JEQZ_IMM32:
                LowerJumpIfZero();
                break;
            case kungfu::EcmaOpcode::JNEZ_IMM8:
            case kungfu::EcmaOpcode::JNEZ_IMM16:
            case kungfu::EcmaOpcode::JNEZ_IMM32:
                LowerJumpIfNonZero();
                break;
            case kungfu::EcmaOpcode::JMP_IMM8:
            case kungfu::EcmaOpcode::JMP_IMM16:
            case kungfu::EcmaOpcode::JMP_IMM32:
                LowerJumpConstant();
                break;
            case kungfu::EcmaOpcode::RETURNUNDEFINED:
                self->FinishBlockWith<ReturnVertex>(currentBlock, {self->undefinedValue_});
                break;
            case kungfu::EcmaOpcode::RETURN:
                self->FinishBlockWith<ReturnVertex>(currentBlock, {frameState.GetAcc()});
                break;
            default:
                UNREACHABLE();
        }
        return true;
    }

    void EmitUnconditionalDeopt()
    {
        currentBlock->SetDeferred(true);
        constexpr auto DEOPT_TYPE = kungfu::DeoptType::INSUFFICIENTPROFILE;
        uint32_t bytecodeOffset = self->preproc_->GetBytecodeOffset(currentBcIndex);
        auto *deopt = self->FinishBlockWith<DeoptVertex>(currentBlock, {}, self->chunk_, DEOPT_TYPE, bytecodeOffset);
        deopt->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(currentBcIndex));
    }

    // -------- Category #2: Constant Loads --------

    void LowerLdTaggedConstant(JSTaggedType taggedValue)
    {
        frameState.SetAcc(self->graph_->GetTaggedConstant(taggedValue));
    }

    void LowerLdaiImm32(const BytecodeInfo *bcInfo)
    {
        frameState.SetAcc(TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 0)));
    }

    void LowerFldaiImm64(const BytecodeInfo *bcInfo)
    {
        JSTaggedType taggedValue = JSTaggedValue(base::bit_cast<double>(GetImmediate(bcInfo, 0))).GetRawData();
        frameState.SetAcc(self->graph_->GetTaggedConstant(taggedValue));
    }

    void LowerLdString(const BytecodeInfo *bcInfo)
    {
        ValueVertex *stringId = self->graph_->GetInt32Constant(GetConstDataId<int>(bcInfo, 0));
        ValueVertex *res = StringFromConstPool(stringId);
        compileInfoFacts_->EnsureType(res, NodeInfo::NodeType::STRING);
        frameState.SetAcc(res);
    }

    void LowerLdBigInt(const BytecodeInfo *bcInfo)
    {
        ValueVertex *stringId = self->graph_->GetInt32Constant(GetConstDataId<int>(bcInfo, 0));
        ValueVertex *numberBigInt = StringFromConstPool(stringId);
        RuntimeCallToAccWithLazyDeopt({numberBigInt}, RTSTUB_ID(LdBigInt));
    }

    // -------- Category #3: Unary Arithmetic --------

    void LowerInc()
    {
        ValueVertex *value = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldUnaryConstant(value, UnaryFoldOp::INC, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildUnaryOperation(CommonStubID::Inc);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerDec()
    {
        ValueVertex *value = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldUnaryConstant(value, UnaryFoldOp::DEC, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildUnaryOperation(CommonStubID::Dec);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerNeg()
    {
        ValueVertex *value = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldUnaryConstant(value, UnaryFoldOp::NEG, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildUnaryOperation(CommonStubID::Neg);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerNot()
    {
        ValueVertex *value = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldUnaryConstant(value, UnaryFoldOp::NOT, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildUnaryOperation(CommonStubID::Not);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    // -------- Category #4: Binary Arithmetic --------

    ValueVertex *TaggedConstantFromFoldedValue(JSTaggedValue value)
    {
        ValueVertex *constant = self->graph_->GetTaggedConstant(value.GetRawData());
        compileInfoFacts_->EnsureType(constant, NodeTypeFromJSTaggedValue(value));
        return constant;
    }

    void LowerAdd2(const BytecodeInfo *bcInfo)
    {
        ValueVertex *x = LoadRegister(bcInfo, 0);
        ValueVertex *y = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldBinaryConstant(x, y, BinaryFoldOp::ADD, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildBinaryOperation(BinaryOpKind::ADD);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerSub2(const BytecodeInfo *bcInfo)
    {
        ValueVertex *x = LoadRegister(bcInfo, 0);
        ValueVertex *y = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldBinaryConstant(x, y, BinaryFoldOp::SUB, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildBinaryOperation(BinaryOpKind::SUB);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerMul2(const BytecodeInfo *bcInfo)
    {
        ValueVertex *x = LoadRegister(bcInfo, 0);
        ValueVertex *y = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldBinaryConstant(x, y, BinaryFoldOp::MUL, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildBinaryOperation(BinaryOpKind::MUL);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerDiv2(const BytecodeInfo *bcInfo)
    {
        ValueVertex *x = LoadRegister(bcInfo, 0);
        ValueVertex *y = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldBinaryConstant(x, y, BinaryFoldOp::DIV, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildBinaryOperation(BinaryOpKind::DIV);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerMod2(const BytecodeInfo * /*bcInfo*/)
    {
        ValueVertex *result = BuildBinaryOperation(BinaryOpKind::MOD);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerExp(const BytecodeInfo *bcInfo)
    {
        ValueVertex *left = LoadRegister(bcInfo, 0);
        ValueVertex *right = frameState.GetAcc();
        RuntimeCallToAccWithLazyDeopt({left, right}, RTSTUB_ID(Exp));
    }

    void LowerShl2(const BytecodeInfo *bcInfo)
    {
        ValueVertex *x = LoadRegister(bcInfo, 0);
        ValueVertex *y = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldBinaryConstant(x, y, BinaryFoldOp::SHL, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildBitwiseOperation(IntBitwiseKind::SHIFT_LEFT);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerShr2(const BytecodeInfo *bcInfo)
    {
        ValueVertex *x = LoadRegister(bcInfo, 0);
        ValueVertex *y = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldBinaryConstant(x, y, BinaryFoldOp::SHR, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildBitwiseOperation(IntBitwiseKind::SHIFT_RIGHT_LOGICAL);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerAshr2(const BytecodeInfo *bcInfo)
    {
        ValueVertex *x = LoadRegister(bcInfo, 0);
        ValueVertex *y = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldBinaryConstant(x, y, BinaryFoldOp::ASHR, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildBitwiseOperation(IntBitwiseKind::SHIFT_RIGHT_ARITHMETIC);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerAnd2(const BytecodeInfo *bcInfo)
    {
        ValueVertex *x = LoadRegister(bcInfo, 0);
        ValueVertex *y = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldBinaryConstant(x, y, BinaryFoldOp::AND, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildBitwiseOperation(IntBitwiseKind::BITWISE_AND);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerOr2(const BytecodeInfo *bcInfo)
    {
        ValueVertex *x = LoadRegister(bcInfo, 0);
        ValueVertex *y = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldBinaryConstant(x, y, BinaryFoldOp::OR, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildBitwiseOperation(IntBitwiseKind::BITWISE_OR);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerXor2(const BytecodeInfo *bcInfo)
    {
        ValueVertex *x = LoadRegister(bcInfo, 0);
        ValueVertex *y = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldBinaryConstant(x, y, BinaryFoldOp::XOR, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        ValueVertex *result = BuildBitwiseOperation(IntBitwiseKind::BITWISE_XOR);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    // -------- Category #5: Comparisons --------

    bool TryFoldCompareAtBytecode(const BytecodeInfo *bcInfo, BinaryFoldOp foldOp)
    {
        ValueVertex *x = LoadRegister(bcInfo, 0);
        ValueVertex *y = frameState.GetAcc();
        JSTaggedValue folded;
        if (!TryFoldBinaryConstant(x, y, foldOp, &folded)) {
            return false;
        }
        frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
        return true;
    }

    void LowerEq(const BytecodeInfo *bcInfo)
    {
        if (TryFoldCompareAtBytecode(bcInfo, BinaryFoldOp::EQ)) {
            return;
        }
        ValueVertex *result = BuildCompareOperation(JSCondition::EQUAL);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerNotEq(const BytecodeInfo *bcInfo)
    {
        if (TryFoldCompareAtBytecode(bcInfo, BinaryFoldOp::NOT_EQ)) {
            return;
        }
        ValueVertex *result = BuildCompareOperation(JSCondition::NOT_EQUAL);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerLess(const BytecodeInfo *bcInfo)
    {
        if (TryFoldCompareAtBytecode(bcInfo, BinaryFoldOp::LESS)) {
            return;
        }
        ValueVertex *result = BuildCompareOperation(JSCondition::LESS_THAN);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerLessEq(const BytecodeInfo *bcInfo)
    {
        if (TryFoldCompareAtBytecode(bcInfo, BinaryFoldOp::LESS_EQ)) {
            return;
        }
        ValueVertex *result = BuildCompareOperation(JSCondition::LESS_THAN_OR_EQUAL);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerGreater(const BytecodeInfo *bcInfo)
    {
        if (TryFoldCompareAtBytecode(bcInfo, BinaryFoldOp::GREATER)) {
            return;
        }
        ValueVertex *result = BuildCompareOperation(JSCondition::GREATER_THAN);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerGreaterEq(const BytecodeInfo *bcInfo)
    {
        if (TryFoldCompareAtBytecode(bcInfo, BinaryFoldOp::GREATER_EQ)) {
            return;
        }
        ValueVertex *result = BuildCompareOperation(JSCondition::GREATER_THAN_OR_EQUAL);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerStrictNotEq(const BytecodeInfo *bcInfo)
    {
        if (TryFoldCompareAtBytecode(bcInfo, BinaryFoldOp::STRICT_NOT_EQ)) {
            return;
        }
        ValueVertex *result = BuildCompareOperation(JSCondition::STRICT_NOT_EQUAL);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerStrictEq(const BytecodeInfo *bcInfo)
    {
        if (TryFoldCompareAtBytecode(bcInfo, BinaryFoldOp::STRICT_EQ)) {
            return;
        }
        ValueVertex *result = BuildCompareOperation(JSCondition::STRICT_EQUAL);
        frameState.SetAcc(result);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, result);
    }

    void LowerIsTrue()
    {
        LowerToBoolean(true);
    }

    void LowerIsFalse()
    {
        LowerToBoolean(false);
    }

    void LowerToBoolean(bool trueIfTruthy)
    {
        frameState.SetAcc(BuildToBoolean(frameState.GetAcc(), trueIfTruthy));
    }

    ValueVertex *TryBuildBooleanWithoutCall(ValueVertex *value, bool trueIfTruthy)
    {
        bool toBoolean = false;
        if (TryFoldToBooleanConstant(value, &toBoolean)) {
            return GetBooleanConstant(trueIfTruthy ? toBoolean : !toBoolean);
        }
        // Static type facts are guaranteed by checked producers: no guard is needed.
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::BOOLEAN)) {
            return trueIfTruthy ? value : BuildTaggedNotEqual(value, GetBooleanConstant(true));
        }
        if (IsAlwaysTruthyHeapObject(value)) {
            return GetBooleanConstant(trueIfTruthy);
        }
        ValueVertex *result = TryBuildKnownIntToBoolean(value, trueIfTruthy);
        if (result == nullptr && compileInfoFacts_->CheckType(value, NodeInfo::NodeType::NUMBER)) {
            result = BuildNumberToBoolean(value, trueIfTruthy);
        }
        if (result != nullptr) {
            compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
        }
        return result;
    }

    ValueVertex *BuildToBoolean(ValueVertex *value, bool trueIfTruthy)
    {
        ValueVertex *result = TryBuildBooleanWithoutCall(value, trueIfTruthy);
        if (result == nullptr) {
            OperationFeedback feedback = self->pgoContext_.ReadOperationFeedback(currentBcInfo);
            switch (feedback.hint) {
                case ArkSteedOperationHint::BOOLEAN:
                    BuildDeoptIfNotBoolean(value);
                    compileInfoFacts_->EnsureType(value, NodeInfo::NodeType::BOOLEAN);
                    result = trueIfTruthy ? value : BuildTaggedNotEqual(value, GetBooleanConstant(true));
                    break;
                case ArkSteedOperationHint::UNDEFINED_OR_NULL:
                    BuildDeoptIfNotUndefinedOrNull(value);
                    compileInfoFacts_->EnsureType(value, NodeInfo::NodeType::NULL_OR_UNDEFINED);
                    result = GetBooleanConstant(!trueIfTruthy);
                    break;
                case ArkSteedOperationHint::INT:
                    result = BuildI32ToBoolean(BuildCheckedTaggedIntToI32(value), trueIfTruthy);
                    break;
                case ArkSteedOperationHint::NUMBER:
                    // Double-only sites skip the int arm of the fused vertex;
                    // mixed int/double sites keep it to avoid deopt loops.
                    if (feedback.IsDoubleOnly()) {
                        result = BuildTaggedDoubleToBoolean(value, trueIfTruthy);
                    } else {
                        result = BuildNumberToBoolean(value, trueIfTruthy);
                    }
                    break;
                default:
                    result = CommonStubCall({glue, value},
                                            trueIfTruthy ? CommonStubID::ToBooleanTrue : CommonStubID::ToBooleanFalse,
                                            SideEffectKind::SAFE_CALL);
                    break;
            }
        }
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
        return result;
    }

    bool IsAlwaysTruthyHeapObject(ValueVertex *value) const
    {
        // SYMBOL and JS_RECEIVER exclude string ("" is falsy) and bigint (0n is falsy),
        // and ArkTS has no undetectable objects: ToBoolean of such values is constant true.
        constexpr NodeInfo::NodeType alwaysTruthy =
            NodeInfo::UnionNodeType(NodeInfo::NodeType::SYMBOL, NodeInfo::NodeType::JS_RECEIVER);
        return compileInfoFacts_->CheckType(value, alwaysTruthy);
    }

    // -------- Category #6: Type Conversions --------

    ValueVertex *BuildNumberToBoolean(ValueVertex *value, bool trueIfNonZero)
    {
        // Values already materialized as f64 use it directly; the tagged
        // form would require re-boxing.
        if (ValueVertex *f64 = compileInfoFacts_->TryGetAlternative(value, AlternativeNodes::Kind::HOLEY_FLOAT64)) {
            ValueVertex *result = self->NewVertex<Float64ToBooleanVertex>(
                compileInfoFacts_, currentBlock, {f64}, trueIfNonZero);
            compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
            return result;
        }
        // Otherwise the fused vertex: the tag dispatch is the NOTNUMBER1
        // guard, ints test the raw low-32 bits, tagged doubles decode in
        // place for the branchless magnitude trick.
        std::vector<ValueVertex *> inputs {value};
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        ValueVertex *result = self->NewVertex<TaggedNumberToBooleanVertex>(
            currentBlock, inputs, self->chunk_, self->preproc_->GetBytecodeOffset(currentBcIndex), trueIfNonZero);
        result->Cast<TaggedNumberToBooleanVertex>()->SetEagerDeoptFrameState(std::move(deoptFrameState));
        compileInfoFacts_->EnsureType(value, NodeInfo::NodeType::NUMBER);
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
        return result;
    }

    ValueVertex *BuildTaggedDoubleToBoolean(ValueVertex *value, bool trueIfNonZero)
    {
        // Values already materialized as f64 use it directly; the tagged
        // form would require re-boxing.
        if (ValueVertex *f64 = compileInfoFacts_->TryGetAlternative(value, AlternativeNodes::Kind::HOLEY_FLOAT64)) {
            ValueVertex *result = self->NewVertex<Float64ToBooleanVertex>(
                compileInfoFacts_, currentBlock, {f64}, trueIfNonZero);
            compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
            return result;
        }
        // Double-only site: the tag check is the NOTDOUBLE1 guard, the decoded
        // payload feeds the branchless magnitude trick.
        std::vector<ValueVertex *> inputs {value};
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        ValueVertex *result = self->NewVertex<TaggedDoubleToBooleanVertex>(
            currentBlock, inputs, self->chunk_, self->preproc_->GetBytecodeOffset(currentBcIndex), trueIfNonZero);
        result->Cast<TaggedDoubleToBooleanVertex>()->SetEagerDeoptFrameState(std::move(deoptFrameState));
        compileInfoFacts_->EnsureType(value, NodeInfo::NodeType::DOUBLE);
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
        return result;
    }

    ValueVertex *BuildBooleanToNumber(ValueVertex *value)
    {
        ValueVertex *raw = self->NewVertex<TaggedToRawI64Vertex>(compileInfoFacts_, currentBlock, {value});
        ValueVertex *maskOne = self->graph_->GetInt64Constant(1);
        ValueVertex *masked = self->NewVertex<I64BitwiseBinaryVertex>(
            compileInfoFacts_, currentBlock, {raw, maskOne}, IntBitwiseKind::BITWISE_AND);
        ValueVertex *tagInt = self->graph_->GetInt64Constant(static_cast<int64_t>(JSTaggedValue::TAG_INT));
        ValueVertex *taggedIntRaw = self->NewVertex<I64BitwiseBinaryVertex>(
            compileInfoFacts_, currentBlock, {masked, tagInt}, IntBitwiseKind::BITWISE_OR);
        ValueVertex *result = self->NewVertex<RawI64ToTaggedVertex>(compileInfoFacts_, currentBlock, {taggedIntRaw});
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::INT);
        return result;
    }

    ValueVertex *BuildUndefinedOrNullToNumber(ValueVertex *value)
    {
        ValueVertex *raw = self->NewVertex<TaggedToRawI64Vertex>(compileInfoFacts_, currentBlock, {value});
        ValueVertex *undefinedValue =
            self->graph_->GetInt64Constant(static_cast<int64_t>(JSTaggedValue::VALUE_UNDEFINED));
        ValueVertex *nanValue = self->graph_->GetTaggedConstant(base::NumberHelper::GetNaN());
        ValueVertex *zeroValue = self->graph_->GetTaggedConstant(JSTaggedValue::VALUE_ZERO);
        ValueVertex *result = BuildSelect<BranchIfInt64CompareVertex>(
            {raw, undefinedValue}, self->AccIndex(),
            [&]() -> ValueVertex * { return nanValue; },
            [&]() -> ValueVertex * { return zeroValue; },
            Condition::EQUAL);
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::NUMBER);
        return result;
    }

    // Shared PGO specialization for ToNumber/ToNumeric: boolean and
    // null/undefined sites convert inline, number sites pass through behind an
    // eager-deopt guard, and ToNumeric additionally passes bigint through.
    // Polymorphic bits never match a single hint, so such sites conservatively
    // fall back to the runtime call. Returns false when the feedback does not
    // justify a specialization.
    bool TrySpecializeNumericConversion(ValueVertex *value, bool allowBigInt)
    {
        OperationFeedback feedback = self->pgoContext_.ReadOperationFeedback(currentBcInfo);
        switch (feedback.hint) {
            case ArkSteedOperationHint::BOOLEAN:
                BuildDeoptIfNotBoolean(value);
                frameState.SetAcc(BuildBooleanToNumber(value));
                return true;
            case ArkSteedOperationHint::UNDEFINED_OR_NULL:
                BuildDeoptIfNotUndefinedOrNull(value);
                frameState.SetAcc(BuildUndefinedOrNullToNumber(value));
                return true;
            case ArkSteedOperationHint::INT:
            case ArkSteedOperationHint::NUMBER:
                BuildDeoptIfNotNumber(value);
                frameState.SetAcc(value);
                return true;
            case ArkSteedOperationHint::BIG_INT:
                if (allowBigInt) {
                    BuildDeoptIfNotBigInt(value);
                    frameState.SetAcc(value);
                    return true;
                }
                return false;
            default:
                return false;
        }
    }

    void LowerToNumber()
    {
        ValueVertex *value = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldUnaryConstant(value, UnaryFoldOp::TO_NUMBER, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::NUMBER)) {
            frameState.SetAcc(value);
            return;
        }
        if (TrySpecializeNumericConversion(value, false)) {
            return;
        }
        RuntimeCallToAccWithLazyDeopt({value}, RTSTUB_ID(ToNumber));
    }

    void LowerToNumeric()
    {
        ValueVertex *value = frameState.GetAcc();
        JSTaggedValue folded;
        if (TryFoldUnaryConstant(value, UnaryFoldOp::TO_NUMERIC, &folded)) {
            frameState.SetAcc(TaggedConstantFromFoldedValue(folded));
            return;
        }
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::NUMBER) ||
            compileInfoFacts_->CheckType(value, NodeInfo::NodeType::BIGINT)) {
            frameState.SetAcc(value);
            return;
        }
        if (TrySpecializeNumericConversion(value, true)) {
            return;
        }
        RuntimeCallToAccWithLazyDeopt({value}, RTSTUB_ID(ToNumeric));
    }

    void LowerToPropertyKey()
    {
        ValueVertex *value = frameState.GetAcc();
        RuntimeCallToAccWithLazyDeopt({value}, RTSTUB_ID(ToPropertyKey));
    }

    // -------- Category #7: Property Access --------

    void LowerLdObjByName(const BytecodeInfo *bcInfo, uint32_t bcIndex)
    {
        ValueVertex *receiver = frameState.GetAcc();
        uint16_t constDataId = GetConstDataId(bcInfo, 1);
        if (TryBuildLoadNamedProperty(bcInfo, bcIndex, receiver, constDataId)) {
            return;
        }
        ValueVertex *id = self->graph_->GetIntPtrConstant(static_cast<intptr_t>(constDataId));
        CommonStubCallToAccWithICAndLazyDeopt(bcInfo, {receiver, id, GlobalEnv()}, CommonStubID::GetPropertyByName);
    }

    void LowerStObjByName(const BytecodeInfo *bcInfo, uint32_t bcIndex)
    {
        ValueVertex *receiver = LoadRegister(bcInfo, 2);  // 2: receiver register index
        uint16_t constDataId = GetConstDataId(bcInfo, 1);
        ValueVertex *value = frameState.GetAcc();

        NamedStoreAccessSet access;
        auto factory = self->pgoContext_.CreateAccessInfoFactory(bcInfo);
        if (factory.TryBuildNamedStoreAccessInfo(0, &access) &&
            TryLowerNamedStoreAccessSet(bcIndex, access, receiver, value, PropertyKey::ConstDataId(constDataId))) {
            return;
        }
        if (TryBuildStoreNamedProperty(bcIndex, receiver, constDataId, value)) {
            return;
        }
        ValueVertex *id = self->graph_->GetIntPtrConstant(static_cast<intptr_t>(constDataId));
        CommonStubCallWithICAndLazyDeopt(bcInfo, {receiver, id, value, GlobalEnv()}, CommonStubID::SetPropertyByName);
    }

    void LowerLdObjByValue(const BytecodeInfo *bcInfo, uint32_t bcIndex)
    {
        ValueVertex *receiver = LoadRegister(bcInfo, 1);
        ValueVertex *key = frameState.GetAcc();
        if (TryFoldConstantStringElement(receiver, key)) {
            return;
        }
        if (TryBuildLoadPropertyByValue(bcInfo, bcIndex, receiver, key)) {
            return;
        }
        CommonStubCallToAccWithICAndLazyDeopt(bcInfo, {receiver, key, GlobalEnv()}, CommonStubID::GetPropertyByValue);
    }

    void LowerStObjByValue(const BytecodeInfo *bcInfo, uint32_t bcIndex)
    {
        ValueVertex *receiver = LoadRegister(bcInfo, 1);
        ValueVertex *key = LoadRegister(bcInfo, 2);  // 2: key register index
        ValueVertex *value = frameState.GetAcc();
        ElementStoreAccessInfo access;
        auto factory = self->pgoContext_.CreateAccessInfoFactory(bcInfo);
        if (factory.TryBuildElementStoreAccessInfo(0, &access) &&
            TryLowerElementStoreInBounds(bcIndex, access, receiver, key, value)) {
            return;
        }
        CommonStubCallWithICAndLazyDeopt(bcInfo, {receiver, key, value, GlobalEnv()}, CommonStubID::SetPropertyByValue);
    }

    void LowerLdObjByIndex(const BytecodeInfo *bcInfo)
    {
        ValueVertex *receiver = LoadRegister(bcInfo, 1);
        ValueVertex *index = self->graph_->GetInt32Constant(GetImmediate<int>(bcInfo, 0));
        CommonStubCallToAccWithLazyDeopt({glue, receiver, index, GlobalEnv()}, CommonStubID::LdObjByIndex);
    }

    void LowerStObjByIndex(const BytecodeInfo *bcInfo)
    {
        ValueVertex *receiver = LoadRegister(bcInfo, 0);
        ValueVertex *index = self->graph_->GetInt32Constant(GetImmediate<int>(bcInfo, 1));
        ValueVertex *value = frameState.GetAcc();
        CommonStubCallWithLazyDeopt({glue, receiver, index, value, GlobalEnv()}, CommonStubID::StObjByIndex);
    }

    void LowerLdThisByValue(const BytecodeInfo *bcInfo, uint32_t bcIndex)
    {
        ValueVertex *receiver = LoadParam(THIS_OBJECT_PARAM_INDEX);
        ValueVertex *key = frameState.GetAcc();
        if (TryBuildLoadPropertyByValue(bcInfo, bcIndex, receiver, key)) {
            return;
        }
        CommonStubCallToAccWithICAndLazyDeopt(bcInfo, {receiver, key, GlobalEnv()}, CommonStubID::GetPropertyByValue);
    }

    void LowerStThisByValue(const BytecodeInfo *bcInfo, uint32_t bcIndex)
    {
        ValueVertex *receiver = LoadParam(THIS_OBJECT_PARAM_INDEX);
        ValueVertex *key = LoadRegister(bcInfo, 1);
        ValueVertex *value = frameState.GetAcc();
        ElementStoreAccessInfo access;
        auto factory = self->pgoContext_.CreateAccessInfoFactory(bcInfo);
        if (factory.TryBuildElementStoreAccessInfo(0, &access) &&
            TryLowerElementStoreInBounds(bcIndex, access, receiver, key, value)) {
            return;
        }
        CommonStubCallWithICAndLazyDeopt(bcInfo, {receiver, key, value, GlobalEnv()}, CommonStubID::SetPropertyByValue);
    }

    void LowerLdThisByName(const BytecodeInfo *bcInfo)
    {
        ValueVertex *receiver = LoadParam(THIS_OBJECT_PARAM_INDEX);
        ValueVertex *id = self->graph_->GetIntPtrConstant(GetConstDataId<intptr_t>(bcInfo, 1));
        CommonStubCallToAccWithICAndLazyDeopt(bcInfo, {receiver, id, GlobalEnv()}, CommonStubID::GetPropertyByName);
    }

    void LowerStThisByName(const BytecodeInfo *bcInfo)
    {
        ValueVertex *receiver = LoadParam(THIS_OBJECT_PARAM_INDEX);
        ValueVertex *id = self->graph_->GetIntPtrConstant(GetConstDataId<intptr_t>(bcInfo, 1));
        ValueVertex *value = frameState.GetAcc();
        CommonStubCallWithICAndLazyDeopt(bcInfo, {receiver, id, value, GlobalEnv()}, CommonStubID::SetPropertyByName);
    }

    void LowerLdSuperByValue(const BytecodeInfo *bcInfo)
    {
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *thisObj = LoadRegister(bcInfo, 0);
        ValueVertex *propKey = frameState.GetAcc();
        RuntimeCallToAccWithLazyDeopt({thisObj, propKey, jsFunc}, RTSTUB_ID(OptLdSuperByValue));
    }

    void LowerStSuperByValue(const BytecodeInfo *bcInfo)
    {
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *thisObj = LoadRegister(bcInfo, 0);
        ValueVertex *propKey = LoadRegister(bcInfo, 1);
        ValueVertex *value = frameState.GetAcc();
        RuntimeCallWithLazyDeopt({thisObj, propKey, value, jsFunc}, RTSTUB_ID(OptStSuperByValue));
    }

    void LowerLdSuperByName(const BytecodeInfo *bcInfo)
    {
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *thisObj = frameState.GetAcc();
        ValueVertex *stringId = self->graph_->GetInt32Constant(GetConstDataId<int>(bcInfo, 0));
        ValueVertex *prop = StringFromConstPool(stringId);
        RuntimeCallToAccWithLazyDeopt({thisObj, prop, jsFunc}, RTSTUB_ID(OptLdSuperByValue));
    }

    void LowerStSuperByName(const BytecodeInfo *bcInfo)
    {
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *thisObj = LoadRegister(bcInfo, 1);
        ValueVertex *value = frameState.GetAcc();
        ValueVertex *stringId = self->graph_->GetInt32Constant(GetConstDataId<int>(bcInfo, 0));
        ValueVertex *prop = StringFromConstPool(stringId);
        RuntimeCallWithLazyDeopt({thisObj, prop, value, jsFunc}, RTSTUB_ID(OptStSuperByValue));
    }

    void LowerStOwnByValue(const BytecodeInfo *bcInfo)
    {
        ValueVertex *receiver = LoadRegister(bcInfo, 0);
        ValueVertex *key = LoadRegister(bcInfo, 1);
        ValueVertex *value = frameState.GetAcc();
        CommonStubCallWithLazyDeopt({glue, receiver, key, value, GlobalEnv()}, CommonStubID::StOwnByValue);
    }

    void LowerStOwnByIndex(const BytecodeInfo *bcInfo, uint32_t bcIndex)
    {
        ValueVertex *receiver = LoadRegister(bcInfo, 1);  // 1: receiver register index
        int indexValue = GetImmediate<int>(bcInfo, 2);  // 2: immediate index
        ValueVertex *value = frameState.GetAcc();
        ElementStoreAccessInfo access;
        auto factory = self->pgoContext_.CreateAccessInfoFactory(bcInfo);
        if (factory.TryBuildElementStoreAccessInfo(0, &access) &&
            TryLowerElementStoreAppend(bcIndex, access, receiver, TaggedConstantFromInt32(indexValue), value,
                                       CommonStubID::StOwnByIndex)) {
            return;
        }
        ValueVertex *index = self->graph_->GetInt32Constant(indexValue);
        CommonStubCallWithLazyDeopt({glue, receiver, index, value, GlobalEnv()}, CommonStubID::StOwnByIndex);
    }

    void LowerWideStOwnByIndex(const BytecodeInfo *bcInfo)
    {
        ValueVertex *receiver = LoadRegister(bcInfo, 0);
        ValueVertex *index = self->graph_->GetInt32Constant(GetImmediate<int>(bcInfo, 1));
        ValueVertex *value = frameState.GetAcc();
        CommonStubCallWithLazyDeopt({glue, receiver, index, value, GlobalEnv()}, CommonStubID::StOwnByIndex);
    }

    void LowerStOwnByName(const BytecodeInfo *bcInfo)
    {
        ValueVertex *stringId = self->graph_->GetInt32Constant(GetConstDataId<int>(bcInfo, 0));
        ValueVertex *propKey = StringFromConstPool(stringId);
        ValueVertex *receiver = LoadRegister(bcInfo, 1);
        ValueVertex *accValue = frameState.GetAcc();
        CommonStubCallWithLazyDeopt({glue, receiver, propKey, accValue, GlobalEnv()}, CommonStubID::StOwnByName);
    }

    void LowerStOwnByValueWithNameSet(const BytecodeInfo *bcInfo)
    {
        ValueVertex *receiver = LoadRegister(bcInfo, 0);
        ValueVertex *propKey = LoadRegister(bcInfo, 1);
        ValueVertex *accValue = frameState.GetAcc();
        CommonStubCallWithLazyDeopt({glue, receiver, propKey, accValue, GlobalEnv()},
                                    CommonStubID::StOwnByValueWithNameSet);
    }

    void LowerStOwnByNameWithNameSet(const BytecodeInfo *bcInfo)
    {
        ValueVertex *stringId = self->graph_->GetInt32Constant(GetConstDataId<int>(bcInfo, 0));
        ValueVertex *propKey = StringFromConstPool(stringId);
        ValueVertex *receiver = LoadRegister(bcInfo, 1);
        ValueVertex *accValue = frameState.GetAcc();
        CommonStubCallWithLazyDeopt({glue, receiver, propKey, accValue, GlobalEnv()},
                                    CommonStubID::StOwnByNameWithNameSet);
    }

    bool TryBuildLoadGlobalCell(uint32_t bcIndex, const BytecodeInfo *bcInfo)
    {
        std::optional<ArkSteedObjectRef> boxRef = TryGetGlobalCellBox(bcInfo);
        if (!boxRef.has_value()) {
            return false;
        }
        ValueVertex *box = GetHeapConstant(boxRef.value());
        if (box == nullptr) {
            return false;
        }
        frameState.SetAcc(BuildGlobalCellBoxValue(bcIndex, box));
        return true;
    }

    bool TryBuildLoadBuiltinObject(uint32_t bcIndex, const BytecodeInfo *bcInfo)
    {
        if (!self->preproc_->GetEnv()->GetJSOptions().IsEnableLoweringBuiltin()) {
            return false;
        }
        ArkSteedHeapBroker *broker = self->pgoContext_.GetBroker();
        size_t builtinIndex = BuiltinIndex::NOT_FOUND;
        if (!broker->TryGetBuiltinIndexFromConstantPool(GetConstDataId<uint16_t>(bcInfo, 1), &builtinIndex)) {
            return false;
        }
        ValueVertex *result =
            self->NewVertex<LoadBuiltinObjectVertex>(compileInfoFacts_, currentBlock, {glue}, builtinIndex);
        ValueVertex *hole = self->graph_->GetTaggedConstant(JSTaggedValue::VALUE_HOLE);
        BuildCheckTaggedCondition(bcIndex, result, hole, Condition::EQUAL, kungfu::DeoptType::BUILTINISHOLE1);
        frameState.SetAcc(result);
        return true;
    }

    bool TryBuildStoreGlobalCell(uint32_t bcIndex, const BytecodeInfo *bcInfo)
    {
        std::optional<ArkSteedObjectRef> boxRef = TryGetGlobalCellBox(bcInfo);
        if (!boxRef.has_value()) {
            return false;
        }
        ValueVertex *box = GetHeapConstant(boxRef.value());
        if (box == nullptr) {
            return false;
        }
        BuildGlobalCellBoxValue(bcIndex, box);
        BuildStoreTaggedField(box, static_cast<int32_t>(PropertyBox::VALUE_OFFSET), frameState.GetAcc());
        return true;
    }

    void LowerTryLdGlobalByName(const BytecodeInfo *bcInfo, uint32_t bcIndex)
    {
        if (!TryBuildLoadBuiltinObject(bcIndex, bcInfo) && !TryBuildLoadGlobalCell(bcIndex, bcInfo)) {
            ValueVertex *id = self->graph_->GetIntPtrConstant(GetConstDataId<intptr_t>(bcInfo, 1));
            CommonStubCallToAccWithICAndLazyDeopt(bcInfo, {id, GlobalEnv()}, CommonStubID::TryLdGlobalByName);
        }
    }

    void LowerTryStGlobalByName(const BytecodeInfo *bcInfo, uint32_t bcIndex)
    {
        if (!TryBuildStoreGlobalCell(bcIndex, bcInfo)) {
            ValueVertex *id = self->graph_->GetIntPtrConstant(GetConstDataId<intptr_t>(bcInfo, 1));
            ValueVertex *value = frameState.GetAcc();
            CommonStubCallWithICAndLazyDeopt(bcInfo, {id, value, GlobalEnv()}, CommonStubID::TryStGlobalByName);
        }
    }

    void LowerLdGlobalVar(const BytecodeInfo *bcInfo, uint32_t bcIndex)
    {
        if (!TryBuildLoadGlobalCell(bcIndex, bcInfo)) {
            ValueVertex *id = self->graph_->GetIntPtrConstant(GetConstDataId<intptr_t>(bcInfo, 1));
            CommonStubCallToAccWithICAndLazyDeopt(bcInfo, {id, GlobalEnv()}, CommonStubID::LdGlobalVar);
        }
    }

    void LowerStGlobalVar(const BytecodeInfo *bcInfo, uint32_t bcIndex)
    {
        if (!TryBuildStoreGlobalCell(bcIndex, bcInfo)) {
            ValueVertex *id = self->graph_->GetIntPtrConstant(GetConstDataId<intptr_t>(bcInfo, 1));
            ValueVertex *value = frameState.GetAcc();
            CommonStubCallWithICAndLazyDeopt(bcInfo, {id, value, GlobalEnv()}, CommonStubID::StGlobalVar);
        }
    }

    void LowerStConstToGlobalRecord(const BytecodeInfo *bcInfo, bool isConst)
    {
        ValueVertex *stringId = self->graph_->GetInt32Constant(GetConstDataId<int>(bcInfo, 0));
        ValueVertex *propKey = StringFromConstPool(stringId);
        ValueVertex *value = frameState.GetAcc();
        ValueVertex *isConstGate = isConst ? self->graph_->GetTaggedConstant(JSTaggedValue::True().GetRawData())
                                           : self->graph_->GetTaggedConstant(JSTaggedValue::False().GetRawData());
        RuntimeCallWithLazyDeopt({propKey, value, isConstGate}, RTSTUB_ID(StGlobalRecord));
    }

    void LowerLdGlobal()
    {
        constexpr int32_t offset = static_cast<int32_t>(GlobalEnv::HEADER_SIZE + GlobalEnv::JS_GLOBAL_OBJECT_INDEX *
                                                                                     JSTaggedValue::TaggedTypeSize());

        frameState.SetAcc(
            self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {GlobalEnv()}, offset));
    }

    void LowerLdSymbol()
    {
        constexpr int32_t offset = static_cast<int32_t>(GlobalEnv::HEADER_SIZE + GlobalEnv::SYMBOL_FUNCTION_INDEX *
                                                                                     JSTaggedValue::TaggedTypeSize());

        frameState.SetAcc(
            self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {GlobalEnv()}, offset));
    }

    void LowerLdPrivateProperty(const BytecodeInfo *bcInfo)
    {
        ValueVertex *levelIndex = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 1));
        ValueVertex *slotIndex = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 2));
        ValueVertex *lexicalEnv = LoadRegister(bcInfo, 3);  // 3: lexicalEnv register index
        ValueVertex *obj = frameState.GetAcc();
        RuntimeCallToAccWithLazyDeopt({lexicalEnv, levelIndex, slotIndex, obj}, RTSTUB_ID(LdPrivateProperty));
    }

    void LowerStPrivateProperty(const BytecodeInfo *bcInfo)
    {
        ValueVertex *levelIndex = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 1));
        ValueVertex *slotIndex = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 2));
        ValueVertex *obj = LoadRegister(bcInfo, 3);         // 3: obj register index
        ValueVertex *lexicalEnv = LoadRegister(bcInfo, 4);  // 4: lexicalEnv register index
        ValueVertex *value = frameState.GetAcc();
        RuntimeCallWithLazyDeopt({lexicalEnv, levelIndex, slotIndex, obj, value}, RTSTUB_ID(StPrivateProperty));
    }

    // -------- Category #8: Function Calls --------

    bool CanUseCallSelf() const
    {
        switch (self->preproc_->GetMethod()->GetFunctionKind()) {
            case FunctionKind::NORMAL_FUNCTION:
            case FunctionKind::ARROW_FUNCTION:
            case FunctionKind::BASE_CONSTRUCTOR:
                return true;
            default:
                return false;
        }
    }

    std::optional<KnownConstructPlan> TryCreateProfiledKnownConstructPlan(const ConstructorFeedback &feedback)
    {
        if (!feedback.IsMonomorphicJSFunction()) {
            return std::nullopt;
        }
        auto *broker = self->pgoContext_.GetBroker();
        ArkSteedHeapBroker::KnownConstructInfo info;
        if (!broker->TryGetKnownConstructInfo(feedback.target, &info)) {
            return std::nullopt;
        }
        ValueVertex *targetConstant = GetHeapConstant(feedback.target);
        if (targetConstant == nullptr) {
            return std::nullopt;
        }
        ValueVertex *hclassConstant = nullptr;
        if (info.initialHClass.IsSafeForCompile()) {
            hclassConstant = GetHeapConstant(info.initialHClass);
            if (hclassConstant == nullptr) {
                return std::nullopt;
            }
        }
        return KnownConstructPlan(info.formalArgc, targetConstant, hclassConstant, info.objectSize,
                                  info.canAllocateDirectly);
    }

    std::optional<KnownCallPlan> TryCreateProfiledKnownCallPlan(const CallFeedback &feedback, uint32_t actualArgc)
    {
        if (!feedback.IsMonomorphicJSFunction()) {
            return std::nullopt;
        }

        ArkSteedHeapBroker *broker = self->pgoContext_.GetBroker();

        if (!broker->CanCallFunction(feedback.target, actualArgc)) {
            return std::nullopt;
        }
        ValueVertex *expectedTargetConstant = GetHeapConstant(feedback.target);
        if (expectedTargetConstant == nullptr) {
            return std::nullopt;
        }
        auto plan = KnownCallPlan::ForFeedback(actualArgc, expectedTargetConstant);
        plan.Verify();
        return plan;
    }

    ValueVertex *TryGetCanonicalBuiltinMethodConstant(kungfu::BuiltinsStubCSigns::ID builtinId,
                                                      uint32_t globalEnvFieldIndex)
    {
        ArkSteedHeapBroker *broker = self->pgoContext_.GetBroker();

        auto method = broker->GetBuiltinMethod(static_cast<uint16_t>(builtinId), globalEnvFieldIndex);
        return method.IsSafeForCompile() ? GetHeapConstant(method) : nullptr;
    }

    std::optional<KnownBuiltinCallPlan> TryCreateProfiledKnownBuiltinCallPlan(
        const CallFeedback &feedback, uint32_t actualArgc)
    {
        if (!feedback.IsMonomorphicBuiltin()) {
            return std::nullopt;
        }

        auto builtinId = feedback.builtinId;
        GlobalEnvField globalEnvField = kungfu::BuiltinsStubCSigns::GetGlobalEnvFieldIndex(builtinId);
        if (globalEnvField == GlobalEnvField::INVALID) {
            return std::nullopt;
        }

        bool usesFastStub = kungfu::BuiltinsStubCSigns::IsFastBuiltin(builtinId) &&
                            builtinId < kungfu::BuiltinsStubCSigns::ID::BUILTINS_CONSTRUCTOR_STUB_FIRST &&
                            actualArgc <= CALL_ARG3;
        BuiltinCallEntryKind entryKind = usesFastStub
            ? BuiltinCallEntryKind::FAST_STUB : BuiltinCallEntryKind::NATIVE_CPP;
        uint32_t globalEnvFieldIndex = static_cast<uint32_t>(globalEnvField);
        ValueVertex *expectedMethodConstant =
            TryGetCanonicalBuiltinMethodConstant(builtinId, globalEnvFieldIndex);
        if (expectedMethodConstant == nullptr) {
            return std::nullopt;
        }
        return KnownBuiltinCallPlan(
            actualArgc, builtinId, globalEnvFieldIndex, entryKind, expectedMethodConstant);
    }

    std::optional<BuiltinConstructPlan> TryCreateProfiledKnownBuiltinConstructPlan(const ConstructorFeedback &feedback,
                                                                                   uint32_t actualArgc)
    {
        if (!feedback.IsMonomorphicBuiltin() || !CanOptimizeBuiltinConstructor(feedback.builtinId)) {
            return std::nullopt;
        }
        size_t globalEnvField = kungfu::BuiltinsStubCSigns::GetGlobalEnvIndex(feedback.builtinId);
        return BuiltinConstructPlan(actualArgc, feedback.builtinId, static_cast<uint32_t>(globalEnvField));
    }

    std::optional<BuiltinConstructPlan> TryCreateBuiltinLoadConstructPlan(ValueVertex *target, uint32_t actualArgc)
    {
        auto *load = target->TryCast<LoadBuiltinObjectVertex>();
        if (load == nullptr || load->GetBuiltinIndex() != static_cast<size_t>(BuiltinType::BT_NUMBER)) {
            return std::nullopt;
        }
        return BuiltinConstructPlan(actualArgc, BuiltinId::NumberConstructor,
                                    static_cast<uint32_t>(GlobalEnv::NUMBER_FUNCTION_INDEX));
    }

    template <class InputRange, class Plan>
    CallKnownJSFunctionVertex *BuildGuardedKnownCall(const InputRange &inputs, const Plan &plan)
    {
        ASSERT(std::size(inputs) == CallVertex::FIRST_ARG_INDEX + plan.GetActualArgc());

        auto input = std::begin(inputs);
        ValueVertex *target = *input++;
        ValueVertex *newTarget = *input++;
        ValueVertex *receiver = *input++;

        ValueVertex *context = self->NewVertex<LoadTaggedFieldVertex>(
            compileInfoFacts_, currentBlock, {target}, JSFunction::LEXICAL_ENV_OFFSET);
        ChunkVector<ValueVertex *> knownInputs(self->chunk_);
        knownInputs.reserve(CallKnownJSFunctionVertex::FIRST_ARG_INDEX + plan.GetActualArgc());
        knownInputs.emplace_back(target);
        knownInputs.emplace_back(newTarget);
        knownInputs.emplace_back(receiver);
        knownInputs.emplace_back(context);
        for (uint32_t index = 0; index < plan.GetActualArgc(); index++) {
            knownInputs.emplace_back(*input++);
        }

        auto *call = self->NewVertex<CallKnownJSFunctionVertex>(
            compileInfoFacts_, currentBlock, knownInputs, plan.GetActualArgc());
        UpdateCatchBlockData(call);
        return call;
    }

    template <class InputRange = std::initializer_list<ValueVertex *>>
    ValueVertex *BuildKnownCall(const InputRange &inputs, const KnownCallPlan &plan)
    {
        ASSERT(std::size(inputs) == CallVertex::FIRST_ARG_INDEX + plan.GetActualArgc());
        ValueVertex *actualTarget = *std::begin(inputs);
        BuildEagerDeoptIfReferenceNotEqual(actualTarget, plan.GetExpectedTargetConstant());
        return BuildGuardedKnownCall(inputs, plan);
    }

    template <class ConstructorVertexT, class InputRange>
    ConstructorVertexT *BuildNewObjRangeCall(const InputRange &inputs, ValueVertex *newTarget, ValueVertex *receiver,
                                             std::optional<uint32_t> expectedArgc,
                                             NewObjRangeCallInfo::CallStrategy strategy)
    {
        uint32_t actualArgc = std::size(inputs) - 1U;
        ChunkVector<ValueVertex *> callInputs(self->chunk_);
        auto input = std::begin(inputs);
        callInputs.emplace_back(*input++);
        callInputs.emplace_back(newTarget);
        callInputs.emplace_back(receiver);
        callInputs.insert(callInputs.end(), input, std::end(inputs));
        auto *call = self->NewVertex<ConstructorVertexT>(compileInfoFacts_, currentBlock, callInputs, actualArgc,
                                                         expectedArgc.value_or(0), expectedArgc.has_value(), strategy);
        UpdateCatchBlockData(call);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
        ASSERT(call->HasLazyDeoptFrameState());
        constexpr auto kind = ConstructorVertexT::IS_BASE_CONSTRUCTOR ? DeoptContinuationKind::BASE_CTOR
                                                                      : DeoptContinuationKind::DERIVED_CTOR;
        auto *kindValue = self->graph_->GetTaggedConstant(EncodeDeoptContinuationKind(kind));
        call->AppendLazyDeoptFrameValue(static_cast<int32_t>(SpecVregIndex::DEOPT_CONTINUATION_KIND_INDEX), kindValue,
                                        DeoptTranslationKind::TAGGED);
        if constexpr (ConstructorVertexT::IS_BASE_CONSTRUCTOR) {
            call->AppendLazyDeoptFrameValue(static_cast<int32_t>(SpecVregIndex::BASE_CONSTRUCTOR_THIS_INDEX), receiver,
                                            DeoptTranslationKind::TAGGED);
        }
        return call;
    }

    template <class InputRange>
    ValueVertex *BuildKnownNewObjRange(const InputRange &inputs, ValueVertex *newTarget, const KnownConstructPlan &plan)
    {
        ValueVertex *target = *std::begin(inputs);
        BuildEagerDeoptIfReferenceNotEqual(target, plan.GetExpectedTargetConstant());
        ValueVertex *hclass = plan.GetHClass();
        if (hclass != nullptr) {
            auto *actualHClass = self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {target},
                                                                        JSFunction::PROTO_OR_DYNCLASS_OFFSET);
            BuildEagerDeoptIfReferenceNotEqual(actualHClass, hclass);
        }

        BB *baseBlock = self->NewBlock();
        BB *derivedBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();
        derivedBlock->SetDeferred(true);
        CompileInfoFacts *entryFacts = compileInfoFacts_;
        self->FinishBlockWithBranch<BranchIfBaseVertex>(currentBlock, {target}, baseBlock, derivedBlock);

        currentBlock = baseBlock;
        compileInfoFacts_ = entryFacts->Clone();
        ValueVertex *receiver;
        if (hclass != nullptr && plan.CanAllocateDirectly()) {
            const uint32_t objectSize = plan.GetObjectSize();
            auto *size = self->graph_->GetInt64Constant(static_cast<int64_t>(objectSize));
            auto *allocation = AllocateInYoung(size, hclass);
            receiver = self->NewVertex<InitializeEmptyObjectVertex>(compileInfoFacts_, currentBlock,
                                                                    {allocation, glue, hclass, size}, objectSize);
        } else {
            receiver = CommonStubCallWithLazyDeopt({glue, target}, CommonStubID::FastNewThisObject);
        }
        auto *baseCall = BuildNewObjRangeCall<CallNewObjRangeBaseVertex>(
            inputs, newTarget, receiver, plan.GetExpectedParameterCount(),
            NewObjRangeCallInfo::CallStrategy::INLINE_DISPATCH);
        CompileInfoFacts *mergedFacts = compileInfoFacts_;
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = derivedBlock;
        compileInfoFacts_ = entryFacts->Clone();
        auto *derivedCall = BuildNewObjRangeCall<CallNewObjRangeDerivedVertex>(
            inputs, newTarget, self->undefinedValue_, plan.GetExpectedParameterCount(),
            NewObjRangeCallInfo::CallStrategy::INLINE_DISPATCH);
        mergedFacts->Merge(*compileInfoFacts_);
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = doneBlock;
        compileInfoFacts_ = mergedFacts;
        return self->NewPhiVertexWith(currentBlock, {baseCall, derivedCall}, self->AccIndex());
    }

    template <class InputRange>
    ValueVertex *BuildDynamicNewObjRange(const InputRange &inputs, ValueVertex *newTarget)
    {
        ValueVertex *target = *std::begin(inputs);
        ValueVertex *receiver = CommonStubCallWithLazyDeopt({glue, target}, CommonStubID::NewThisObjectChecked);
        BB *constructBlock = self->NewBlock();
        BB *fallbackBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();
        fallbackBlock->SetDeferred(true);
        CompileInfoFacts *entryFacts = compileInfoFacts_;
        self->FinishBlockWithBranch<BranchIfReferenceEqualVertex>(
            currentBlock, {receiver, self->graph_->GetTaggedConstant(JSTaggedValue::VALUE_HOLE)}, fallbackBlock,
            constructBlock);

        currentBlock = constructBlock;
        compileInfoFacts_ = entryFacts->Clone();
        BB *baseBlock = self->NewBlock();
        BB *derivedBlock = self->NewBlock();
        BB *constructDone = self->NewBlock();
        self->FinishBlockWithBranch<BranchIfBaseVertex>(currentBlock, {target}, baseBlock, derivedBlock);
        currentBlock = baseBlock;
        compileInfoFacts_ = entryFacts->Clone();
        auto *baseCall = BuildNewObjRangeCall<CallNewObjRangeBaseVertex>(
            inputs, newTarget, receiver, std::nullopt, NewObjRangeCallInfo::CallStrategy::COMMON_STUB);
        CompileInfoFacts *constructFacts = compileInfoFacts_;
        self->FinishBlockWithJump(currentBlock, constructDone);

        currentBlock = derivedBlock;
        compileInfoFacts_ = entryFacts->Clone();
        auto *derivedCall = BuildNewObjRangeCall<CallNewObjRangeDerivedVertex>(
            inputs, newTarget, self->undefinedValue_, std::nullopt, NewObjRangeCallInfo::CallStrategy::COMMON_STUB);
        constructFacts->Merge(*compileInfoFacts_);
        self->FinishBlockWithJump(currentBlock, constructDone);

        currentBlock = constructDone;
        compileInfoFacts_ = constructFacts;
        ValueVertex *constructResult = self->NewPhiVertexWith(currentBlock, {baseCall, derivedCall}, self->AccIndex());
        CompileInfoFacts *mergedFacts = compileInfoFacts_;
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = fallbackBlock;
        compileInfoFacts_ = entryFacts->Clone();
        ValueVertex *fallbackResult = RuntimeCallWithLazyDeopt(inputs, RTSTUB_ID(OptNewObjRange));
        mergedFacts->Merge(*compileInfoFacts_);
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = doneBlock;
        compileInfoFacts_ = mergedFacts;
        return self->NewPhiVertexWith(currentBlock, {constructResult, fallbackResult}, self->AccIndex());
    }

    template <class InputRange = std::initializer_list<ValueVertex *>>
    CallVertex *BuildGenericCallVertex(const InputRange &inputs, uint32_t actualArgc)
    {
        auto *call = self->NewVertex<CallVertex>(compileInfoFacts_, currentBlock, inputs, actualArgc);
        UpdateCatchBlockData(call);
        return call;
    }

    template <class InputRange = std::initializer_list<ValueVertex *>>
    CallSelfVertex *BuildSelfCallVertex(const InputRange &inputs, uint32_t actualArgc)
    {
        ASSERT(std::size(inputs) == CallVertex::FIRST_ARG_INDEX + actualArgc);
        auto input = std::begin(inputs);
        input++;
        ValueVertex *newTarget = *input++;
        ValueVertex *receiver = *input++;
        ChunkVector<ValueVertex *> selfInputs(self->chunk_);
        selfInputs.reserve(CallSelfVertex::FIRST_ARG_INDEX + actualArgc);
        selfInputs.emplace_back(self->initialCallTarget_);
        selfInputs.emplace_back(newTarget);
        selfInputs.emplace_back(receiver);
        selfInputs.emplace_back(self->initialLexicalEnv_);
        for (uint32_t index = 0; index < actualArgc; index++) {
            selfInputs.emplace_back(*input++);
        }

        uint32_t expectedParameterCount = self->preproc_->GetMethod()->GetNumArgsWithCallField();
        auto *call = self->NewVertex<CallSelfVertex>(
            compileInfoFacts_, currentBlock, selfInputs, actualArgc, expectedParameterCount);
        self->graph_->SetHasRecursiveCalls(true);
        UpdateCatchBlockData(call);
        return call;
    }

    template <class InputRange = std::initializer_list<ValueVertex *>>
    ValueVertex *TryBuildPGOSelfCall(const CallFeedback &feedback, const InputRange &inputs, uint32_t actualArgc)
    {
        if (!self->pgoContext_.TargetsCurrentFunction(feedback)) {
            return nullptr;
        }
        ValueVertex *actualTarget = *std::begin(inputs);
        BuildEagerDeoptIfReferenceNotEqual(actualTarget, self->initialCallTarget_);
        return BuildSelfCallVertex(inputs, actualArgc);
    }

    ValueVertex *LoadGlobalEnvValue(uint32_t fieldIndex)
    {
        int32_t fieldOffset =
            static_cast<int32_t>(GlobalEnv::HEADER_SIZE + fieldIndex * JSTaggedValue::TaggedTypeSize());
        return self->NewVertex<LoadTaggedFieldVertex>(
            compileInfoFacts_, currentBlock, {GlobalEnv()}, fieldOffset);
    }

    template <class Plan>
    ValueVertex *LoadCanonicalBuiltinTarget(const Plan &plan)
    {
        return LoadGlobalEnvValue(plan.GetGlobalEnvFieldIndex());
    }

    void BuildEagerDeoptIfReferenceNotEqual(ValueVertex *actual, ValueVertex *expected,
                                            kungfu::DeoptType deoptType = kungfu::DeoptType::NOTCALLTGT1)
    {
        uint32_t bytecodeOffset = self->preproc_->GetBytecodeOffset(currentBcIndex);
        auto *guard = self->NewVertex<DeoptIfReferenceNotEqualVertex>(
            compileInfoFacts_, currentBlock, {actual, expected}, self->chunk_, bytecodeOffset, deoptType);
        guard->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(currentBcIndex));
    }

    ValueVertex *BuildGuardedCanonicalBuiltinTarget(ValueVertex *actualTarget, const KnownBuiltinCallPlan &plan)
    {
        ValueVertex *canonicalTarget = LoadCanonicalBuiltinTarget(plan);
        BuildEagerDeoptIfReferenceNotEqual(actualTarget, canonicalTarget);
        auto *currentMethod = self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {canonicalTarget},
                                                                     JSFunctionBase::METHOD_OFFSET);
        BuildEagerDeoptIfReferenceNotEqual(currentMethod, plan.GetExpectedMethodConstant());
        return canonicalTarget;
    }

    ValueVertex *BuildGuardedCanonicalBuiltinTarget(ValueVertex *actualTarget, const BuiltinConstructPlan &plan)
    {
        ValueVertex *canonicalTarget = LoadCanonicalBuiltinTarget(plan);
        BuildEagerDeoptIfReferenceNotEqual(actualTarget, canonicalTarget,
                                           GetBuiltinConstructorDeoptType(plan.GetGlobalEnvFieldIndex()));
        return canonicalTarget;
    }

    ValueVertex *LoadBuiltinHClass(ValueVertex *constructor)
    {
        return self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {constructor},
                                                      JSFunction::PROTO_OR_DYNCLASS_OFFSET);
    }

    void BuildEagerDeoptIfNotJSHClass(ValueVertex *value, kungfu::DeoptType deoptType)
    {
        uint32_t bytecodeOffset = self->preproc_->GetBytecodeOffset(currentBcIndex);
        auto *guard = self->NewVertex<DeoptIfNotJSHClassVertex>(compileInfoFacts_, currentBlock, {value}, self->chunk_,
                                                                bytecodeOffset, deoptType);
        guard->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(currentBcIndex));
    }

    ValueVertex *BuildObjectFromPrimitive(ValueVertex *value)
    {
        auto *result = CommonStubCallWithLazyDeopt(
            {glue, value, GlobalEnv()}, CommonStubID::ArkSteedNewObjectFromPrimitive);
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::JS_RECEIVER);
        return result;
    }

    template <class InputRange, class Plan>
    CallKnownBuiltinVertex *BuildKnownBuiltinCallVertex(const InputRange &inputs, const Plan &plan,
                                                        ValueVertex *canonicalTarget)
    {
        ASSERT(std::size(inputs) == CallKnownBuiltinVertex::FIRST_ARG_INDEX + plan.GetActualArgc());
        auto input = std::begin(inputs);
        input++;
        ValueVertex *newTarget = *input++;
        ValueVertex *receiver = *input++;

        ChunkVector<ValueVertex *> knownInputs(self->chunk_);
        knownInputs.reserve(CallKnownBuiltinVertex::FIRST_ARG_INDEX + plan.GetActualArgc());
        knownInputs.emplace_back(canonicalTarget);
        knownInputs.emplace_back(newTarget);
        knownInputs.emplace_back(receiver);
        for (uint32_t index = 0; index < plan.GetActualArgc(); index++) {
            knownInputs.emplace_back(*input++);
        }

        auto *call = self->NewVertex<CallKnownBuiltinVertex>(
            compileInfoFacts_, currentBlock, knownInputs, plan.GetActualArgc(), plan.GetBuiltinId(),
            plan.GetEntryKind());
        UpdateCatchBlockData(call);
        return call;
    }

    template <class InputRange = std::initializer_list<ValueVertex *>>
    ValueVertex *BuildKnownBuiltinCall(const InputRange &inputs, const KnownBuiltinCallPlan &plan)
    {
        ASSERT(std::size(inputs) == CallKnownBuiltinVertex::FIRST_ARG_INDEX + plan.GetActualArgc());
        ValueVertex *actualTarget = *std::begin(inputs);
        ValueVertex *canonicalTarget = BuildGuardedCanonicalBuiltinTarget(actualTarget, plan);
        return BuildKnownBuiltinCallVertex(inputs, plan, canonicalTarget);
    }

    template <class InputRange>
    ValueVertex *BuildKnownBuiltinNativeConstruct(const InputRange &callInputs, const BuiltinConstructPlan &plan,
                                                  ValueVertex *canonicalTarget)
    {
        auto *call = BuildKnownBuiltinCallVertex(callInputs, plan, canonicalTarget);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
        compileInfoFacts_->EnsureType(call, NodeInfo::NodeType::JS_RECEIVER);
        return call;
    }

    template <class InputRange>
    ValueVertex *BuildObjectBuiltinConstruct(const InputRange &callInputs, const BuiltinConstructPlan &plan,
                                             ValueVertex *canonicalTarget)
    {
        if (plan.GetActualArgc() == 0) {
            return BuildEmptyObject();
        }

        ValueVertex *argument = *std::next(std::begin(callInputs), CallKnownBuiltinVertex::FIRST_ARG_INDEX);
        if (compileInfoFacts_->CheckType(argument, NodeInfo::NodeType::JS_RECEIVER)) {
            return argument;
        }
        if (compileInfoFacts_->CheckType(argument, NodeInfo::NodeType::SYMBOL) ||
            compileInfoFacts_->CheckType(argument, NodeInfo::NodeType::BIGINT) ||
            compileInfoFacts_->CheckType(argument, NodeInfo::NodeType::NUMBER) ||
            compileInfoFacts_->CheckType(argument, NodeInfo::NodeType::BOOLEAN)) {
            return BuildObjectFromPrimitive(argument);
        }
        if (compileInfoFacts_->CheckType(argument, NodeInfo::NodeType::NULL_OR_UNDEFINED)) {
            return BuildEmptyObject();
        }
        if (compileInfoFacts_->CheckType(argument, NodeInfo::NodeType::STRING)) {
            return BuildKnownBuiltinNativeConstruct(callInputs, plan, canonicalTarget);
        }

        BB *identityBlock = self->NewBlock();
        BB *primitiveBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();

        CompileInfoFacts *entryFacts = compileInfoFacts_;
        self->FinishBlockWithBranch<BranchIfECMAObjectVertex>(currentBlock, {argument}, identityBlock, primitiveBlock);

        currentBlock = identityBlock;
        compileInfoFacts_ = entryFacts->Clone();
        compileInfoFacts_->EnsureType(argument, NodeInfo::NodeType::JS_RECEIVER);
        CompileInfoFacts *mergedFacts = compileInfoFacts_;
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = primitiveBlock;
        compileInfoFacts_ = entryFacts->Clone();
        ValueVertex *wrapped = BuildObjectFromPrimitive(argument);
        mergedFacts->Merge(*compileInfoFacts_);
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = doneBlock;
        compileInfoFacts_ = mergedFacts;
        ValueVertex *result = self->NewPhiVertexWith(currentBlock, {argument, wrapped}, self->AccIndex());
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::JS_RECEIVER);
        return result;
    }

    template <class InputRange>
    ValueVertex *BuildBooleanBuiltinConstruct(const InputRange &callInputs, const BuiltinConstructPlan &plan,
                                              ValueVertex *canonicalTarget)
    {
        ValueVertex *argument = plan.GetActualArgc() == 0
                                    ? self->undefinedValue_
                                    : *std::next(std::begin(callInputs), CallKnownBuiltinVertex::FIRST_ARG_INDEX);
        if (ValueVertex *boolean = TryBuildBooleanWithoutCall(argument, true); boolean != nullptr) {
            return BuildObjectFromPrimitive(boolean);
        }

        NodeInfo::NodeType knownType = compileInfoFacts_->GetKnownType(argument);
        if (!NodeInfo::NodeTypeCanBe(knownType, NodeInfo::NodeType::BOOLEAN)) {
            return BuildKnownBuiltinNativeConstruct(callInputs, plan, canonicalTarget);
        }

        ValueVertex *result = BuildSelectWithFacts<BranchIfTaggedBooleanVertex>(
            {argument}, self->AccIndex(),
            [&]() -> ValueVertex * {
                compileInfoFacts_->EnsureType(argument, NodeInfo::NodeType::BOOLEAN);
                return BuildObjectFromPrimitive(argument);
            },
            [&]() -> ValueVertex * { return BuildKnownBuiltinNativeConstruct(callInputs, plan, canonicalTarget); });
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::JS_RECEIVER);
        return result;
    }

    ValueVertex *BuildNumberStringValue(ValueVertex *argument)
    {
        compileInfoFacts_->EnsureType(argument, NodeInfo::NodeType::STRING);
        ValueVertex *number = CommonStubCallWithLazyDeopt({glue, argument}, CommonStubID::ArkSteedStringToNumber,
                                                          SideEffectKind::SAFE_CALL);
        compileInfoFacts_->EnsureType(number, NodeInfo::NodeType::NUMBER);
        return number;
    }

    template <class InputRange>
    ValueVertex *BuildNumberBuiltinConstruct(const InputRange &callInputs, const BuiltinConstructPlan &plan,
                                             ValueVertex *canonicalTarget)
    {
        if (plan.GetActualArgc() == 0) {
            return BuildObjectFromPrimitive(TaggedConstantFromInt32(0));
        }

        ValueVertex *argument = *std::next(std::begin(callInputs), CallKnownBuiltinVertex::FIRST_ARG_INDEX);
        NodeInfo::NodeType knownType = compileInfoFacts_->GetKnownType(argument);
        if (compileInfoFacts_->CheckType(argument, NodeInfo::NodeType::NUMBER)) {
            return BuildObjectFromPrimitive(argument);
        }
        if (compileInfoFacts_->CheckType(argument, NodeInfo::NodeType::STRING)) {
            ValueVertex *number = BuildNumberStringValue(argument);
            return BuildObjectFromPrimitive(number);
        }
        constexpr NodeInfo::NodeType NUMBER_OR_STRING =
            NodeInfo::UnionNodeType(NodeInfo::NodeType::NUMBER, NodeInfo::NodeType::STRING);
        if (!NodeInfo::NodeTypeCanBe(knownType, NUMBER_OR_STRING)) {
            return BuildKnownBuiltinNativeConstruct(callInputs, plan, canonicalTarget);
        }

        BB *numberBlock = self->NewBlock();
        BB *nonNumberBlock = self->NewBlock();
        BB *stringBlock = self->NewBlock();
        BB *nativeBlock = self->NewBlock();
        BB *allocateBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();
        nativeBlock->SetDeferred(true);

        CompileInfoFacts *entryFacts = compileInfoFacts_;
        auto activate = [&](BB *block) {
            currentBlock = block;
            compileInfoFacts_ = entryFacts->Clone();
        };

        self->FinishBlockWithBranch<BranchIfTaggedNumberVertex>(currentBlock, {argument}, numberBlock, nonNumberBlock);

        activate(numberBlock);
        compileInfoFacts_->EnsureType(argument, NodeInfo::NodeType::NUMBER);
        CompileInfoFacts *numberFacts = compileInfoFacts_;
        self->FinishBlockWithJump(currentBlock, allocateBlock);

        activate(nonNumberBlock);
        self->FinishBlockWithBranch<BranchIfTaggedStringVertex>(currentBlock, {argument}, stringBlock, nativeBlock);

        activate(stringBlock);
        ValueVertex *converted = BuildNumberStringValue(argument);
        numberFacts->Merge(*compileInfoFacts_);
        self->FinishBlockWithJump(currentBlock, allocateBlock);

        activate(nativeBlock);
        ValueVertex *nativeResult = BuildKnownBuiltinNativeConstruct(callInputs, plan, canonicalTarget);
        CompileInfoFacts *resultFacts = compileInfoFacts_;
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = allocateBlock;
        compileInfoFacts_ = numberFacts;
        ValueVertex *number = self->NewPhiVertexWith(currentBlock, {argument, converted}, self->AccIndex());
        ValueVertex *wrapped = BuildObjectFromPrimitive(number);
        resultFacts->Merge(*compileInfoFacts_);
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = doneBlock;
        compileInfoFacts_ = resultFacts;
        ValueVertex *result = self->NewPhiVertexWith(currentBlock, {nativeResult, wrapped}, self->AccIndex());
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::JS_RECEIVER);
        return result;
    }

    template <class InputRange>
    ValueVertex *BuildFloat32ArrayBuiltinConstruct(const InputRange &callInputs, const BuiltinConstructPlan &plan,
                                                   ValueVertex *canonicalTarget)
    {
        ValueVertex *result;
        if (plan.GetActualArgc() == 0) {
            result = CommonStubCallWithLazyDeopt({glue, GlobalEnv()}, CommonStubID::NewFloat32ArrayWithNoArgs);
        } else if (plan.GetActualArgc() == 1) {
            ValueVertex *argument = *std::next(std::begin(callInputs), CallKnownBuiltinVertex::FIRST_ARG_INDEX);
            result = CommonStubCallWithLazyDeopt({glue, canonicalTarget, argument, GlobalEnv()},
                                                 CommonStubID::NewFloat32Array);
        } else {
            result = BuildKnownBuiltinNativeConstruct(callInputs, plan, canonicalTarget);
        }
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::JS_RECEIVER);
        return result;
    }

    template <class InputRange>
    ValueVertex *BuildKnownBuiltinConstruct(const InputRange &inputs, const BuiltinConstructPlan &plan)
    {
        ASSERT(std::size(inputs) == plan.GetActualArgc() + 1U);
        auto input = std::begin(inputs);
        ValueVertex *target = *input++;

        ChunkVector<ValueVertex *> callInputs(self->chunk_);
        callInputs.reserve(CallKnownBuiltinVertex::FIRST_ARG_INDEX + plan.GetActualArgc());
        callInputs.emplace_back(target);
        callInputs.emplace_back(target);
        callInputs.emplace_back(self->undefinedValue_);
        for (uint32_t index = 0; index < plan.GetActualArgc(); index++) {
            callInputs.emplace_back(*input++);
        }

        ValueVertex *canonicalTarget = BuildGuardedCanonicalBuiltinTarget(target, plan);
        if (plan.GetBuiltinId() == BuiltinId::BooleanConstructor) {
            ValueVertex *builtinHClass = LoadBuiltinHClass(canonicalTarget);
            BuildEagerDeoptIfNotJSHClass(builtinHClass, GetBuiltinConstructorDeoptType(plan.GetGlobalEnvFieldIndex()));
        }
        switch (plan.GetBuiltinId()) {
            case BuiltinId::ObjectConstructor:
                return BuildObjectBuiltinConstruct(callInputs, plan, canonicalTarget);
            case BuiltinId::BooleanConstructor:
                return BuildBooleanBuiltinConstruct(callInputs, plan, canonicalTarget);
            case BuiltinId::NumberConstructor:
                return BuildNumberBuiltinConstruct(callInputs, plan, canonicalTarget);
            case BuiltinId::Float32ArrayConstructor:
                return BuildFloat32ArrayBuiltinConstruct(callInputs, plan, canonicalTarget);
            default:
                return BuildKnownBuiltinNativeConstruct(callInputs, plan, canonicalTarget);
        }
    }

    template <class InputRange = std::initializer_list<ValueVertex *>>
    ValueVertex *BuildCallVertex(const BytecodeInfo *bcInfo, const InputRange &inputs, uint32_t actualArgc)
    {
        ASSERT(bcInfo != nullptr);
        ASSERT(std::size(inputs) == CallVertex::FIRST_ARG_INDEX + actualArgc);
        ValueVertex *target = *std::begin(inputs);
        bool canUseCallSelf = CanUseCallSelf();
        if (canUseCallSelf && target == self->initialCallTarget_) {
            return BuildSelfCallVertex(inputs, actualArgc);
        }

        CallFeedback feedback = self->pgoContext_.ReadCallFeedback(bcInfo);
        if (canUseCallSelf) {
            if (ValueVertex *call = TryBuildPGOSelfCall(feedback, inputs, actualArgc); call != nullptr) {
                return call;
            }
        }

        std::optional<KnownCallPlan> knownCallPlan = TryCreateProfiledKnownCallPlan(feedback, actualArgc);
        if (knownCallPlan.has_value()) {
            return BuildKnownCall(inputs, knownCallPlan.value());
        }

        std::optional<KnownBuiltinCallPlan> knownBuiltinPlan =
            TryCreateProfiledKnownBuiltinCallPlan(feedback, actualArgc);
        if (knownBuiltinPlan.has_value()) {
            return BuildKnownBuiltinCall(inputs, knownBuiltinPlan.value());
        }
        return BuildGenericCallVertex(inputs, actualArgc);
    }

    void LowerCallArg0(const BytecodeInfo *bcInfo)
    {
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *undefined = self->undefinedValue_;

        ValueVertex *call = BuildCallVertex(bcInfo, {func, undefined, undefined}, CALL_ARG0);
        frameState.SetAcc(call);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
    }

    void LowerCallArg1(const BytecodeInfo *bcInfo)
    {
        ValueVertex *a0Value = LoadRegister(bcInfo, 0);
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *undefined = self->undefinedValue_;

        ValueVertex *call = BuildCallVertex(bcInfo, {func, undefined, undefined, a0Value}, CALL_ARG1);
        frameState.SetAcc(call);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
    }

    void LowerCallArgs2(const BytecodeInfo *bcInfo)
    {
        ValueVertex *a0Value = LoadRegister(bcInfo, 0);
        ValueVertex *a1Value = LoadRegister(bcInfo, 1);
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *undefined = self->undefinedValue_;

        ValueVertex *call = BuildCallVertex(bcInfo, {func, undefined, undefined, a0Value, a1Value}, CALL_ARG2);
        frameState.SetAcc(call);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
    }

    void LowerCallArgs3(const BytecodeInfo *bcInfo)
    {
        ValueVertex *a0Value = LoadRegister(bcInfo, 0);
        ValueVertex *a1Value = LoadRegister(bcInfo, 1);
        ValueVertex *a2Value = LoadRegister(bcInfo, 2);  // 2: third argument register index
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *undefined = self->undefinedValue_;

        ValueVertex *call = BuildCallVertex(bcInfo, {func, undefined, undefined, a0Value, a1Value, a2Value}, CALL_ARG3);
        frameState.SetAcc(call);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
    }

    void LowerCallThis0(const BytecodeInfo *bcInfo)
    {
        ValueVertex *thisObj = LoadRegister(bcInfo, 0);
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *undefined = self->undefinedValue_;

        ValueVertex *call = BuildCallVertex(bcInfo, {func, undefined, thisObj}, CALL_ARG0);
        frameState.SetAcc(call);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
    }

    void LowerCallInit(const BytecodeInfo *bcInfo)
    {
        ValueVertex *thisObj = LoadRegister(bcInfo, 0);
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *undefined = self->undefinedValue_;

        CallVertex *call = BuildGenericCallVertex({func, undefined, thisObj}, CALL_ARG0);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
    }

    void LowerCallThis1(const BytecodeInfo *bcInfo)
    {
        ValueVertex *thisObj = LoadRegister(bcInfo, 0);
        ValueVertex *a0Value = LoadRegister(bcInfo, 1);
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *undefined = self->undefinedValue_;

        ValueVertex *call = BuildCallVertex(bcInfo, {func, undefined, thisObj, a0Value}, CALL_ARG1);
        frameState.SetAcc(call);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
    }

    void LowerCallThis2(const BytecodeInfo *bcInfo)
    {
        ValueVertex *thisObj = LoadRegister(bcInfo, 0);
        ValueVertex *a0Value = LoadRegister(bcInfo, 1);
        ValueVertex *a1Value = LoadRegister(bcInfo, 2);  // 2: second argument register index
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *undefined = self->undefinedValue_;

        ValueVertex *call = BuildCallVertex(bcInfo, {func, undefined, thisObj, a0Value, a1Value}, CALL_ARG2);
        frameState.SetAcc(call);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
    }

    void LowerCallThis3(const BytecodeInfo *bcInfo)
    {
        ValueVertex *thisObj = LoadRegister(bcInfo, 0);
        ValueVertex *a0Value = LoadRegister(bcInfo, 1);
        ValueVertex *a1Value = LoadRegister(bcInfo, 2);  // 2: second argument register index
        ValueVertex *a2Value = LoadRegister(bcInfo, 3);  // 3: third argument register index
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *undefined = self->undefinedValue_;

        ValueVertex *call = BuildCallVertex(bcInfo, {func, undefined, thisObj, a0Value, a1Value, a2Value}, CALL_ARG3);
        frameState.SetAcc(call);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
    }

    void LowerCallRange(const BytecodeInfo *bcInfo)
    {
        uint32_t inputSize = bcInfo->inputs.size();
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *undefined = self->undefinedValue_;
        ChunkVector<ValueVertex *> args(self->chunk_);
        args.push_back(func);
        args.push_back(undefined);
        args.push_back(undefined);
        for (uint32_t idx = 0; idx < inputSize; idx++) {
            args.push_back(LoadRegister(bcInfo, static_cast<int>(idx)));
        }
        ValueVertex *call = BuildCallVertex(bcInfo, args, inputSize);
        frameState.SetAcc(call);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
    }

    void LowerCallThisRange(const BytecodeInfo *bcInfo)
    {
        // -1 : Skips the receiver
        uint32_t argc = bcInfo->inputs.size() - 1;
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *thisObj = LoadRegister(bcInfo, 0);
        ValueVertex *undefined = self->undefinedValue_;
        ChunkVector<ValueVertex *> args(self->chunk_);
        args.push_back(func);
        args.push_back(undefined);
        args.push_back(thisObj);
        for (uint32_t idx = 0; idx < argc; idx++) {
            args.push_back(LoadRegister(bcInfo, static_cast<int>(idx + 1)));
        }
        ValueVertex *call = BuildCallVertex(bcInfo, args, argc);
        frameState.SetAcc(call);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
    }

    void LowerCallThisWithName(const BytecodeInfo *bcInfo)
    {
        constexpr uint32_t FIXED_INPUTS = 2;
        ASSERT(bcInfo->inputs.size() >= FIXED_INPUTS);
        uint32_t argc = bcInfo->inputs.size() - FIXED_INPUTS;
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *thisObj = LoadRegister(bcInfo, 0);
        CompileInfoFacts *entryFacts = compileInfoFacts_;
        BB *callBlock = self->NewBlock();
        BB *throwBlock = self->NewBlock();
        self->FinishBlockWithBranch<BranchIfCallableVertex>(currentBlock, {func}, callBlock, throwBlock);

        currentBlock = throwBlock;
        compileInfoFacts_ = entryFacts->Clone();
        currentBlock->SetDeferred(true);
        if (!TryBuildColdCatchDeopt()) {
            ValueVertex *stringId = self->graph_->GetInt32Constant(
                GetConstDataId<int>(bcInfo, static_cast<int>(bcInfo->inputs.size() - 1)));
            ValueVertex *funcName = StringFromConstPool(stringId);
            auto *throws = self->FinishBlockWith<ThrowVertex>(
                currentBlock, {funcName, func}, RTSTUB_ID(ThrowNotCallableException));
            UpdateCatchBlockData(throws);
        }

        currentBlock = callBlock;
        compileInfoFacts_ = entryFacts;
        ChunkVector<ValueVertex *> args(self->chunk_);
        args.push_back(func);
        args.push_back(self->undefinedValue_);
        args.push_back(thisObj);
        for (uint32_t idx = 0; idx < argc; idx++) {
            args.push_back(LoadRegister(bcInfo, static_cast<int>(idx + 1)));
        }
        CallVertex *call = BuildGenericCallVertex(args, argc);
        frameState.SetAcc(call);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, call);
    }

    void LowerCallSpread(const BytecodeInfo *bcInfo)
    {
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *thisArg = LoadRegister(bcInfo, 0);
        ValueVertex *argsArray = LoadRegister(bcInfo, 1);

        RuntimeCallToAccWithLazyDeopt({func, thisArg, argsArray}, RTSTUB_ID(CallSpread));
    }

    // Fast/slow path structure for SUPERCALLTHISRANGE, mirroring the
    // interpreter fast path: the super-constructor checks admit exactly a
    // constructor JSFunction as the super, BranchIfBase splits the fast path,
    // and the newTarget admission runs only on the base arm — its conditions
    // are FastSuperAllocateThis's prerequisites, while the derived arm forwards
    // newTarget to the callee as an opaque value it never dereferences (the
    // interpreter's derived arm likewise admits any non-undefined newTarget).
    // Guard-shape failures branch directly into the slow block — no stub call
    // before the guards and no sentinel result inspection; a monomorphic call
    // slot folds the super-ctor checks into one identity eager deopt instead. thisObj for
    // the fast path is produced per arm: a base super
    // constructor allocates via a FastSuperAllocateThis vertex (currently a stub call, a dedicated
    // vertex so the allocation can be lowered inline later), a derived or builtin one gets
    // undefined (no allocation, callee re-initializes this). The fast path enters the super through
    // a CallConstructor vertex (steed frame convention with the caller's newTarget), never through
    // the AOT-era trampolines, which enter steed code with the wrong calling convention. Whether
    // the super has compiled code is left to the codegen (JumpIfFunctionNotCompiled), which routes
    // uncompiled supers to a generic [[Construct]] fallback instead of a second IR guard. The
    // tagged argument array is only built on the slow path.
    ValueVertex *BuildSuperCallWithFastPath(const BytecodeInfo *bcInfo, ValueVertex *func,
                                            ValueVertex *newTarget, uint32_t argc)
    {
        // The super ctor is the current function's [[Prototype]]. `func` is the executing
        // JSFunction (frame a0), so the GetPrototype stub's TypeError branches (non-heap-object,
        // non-EcmaObject) and its JSProxy branch are statically dead here; what remains is the
        // two-load tail (LoadHClass + hclass->prototype), emitted inline below. The loaded value
        // can still be anything (null / object / proxy / function); the guards below route every
        // non-constructor outcome to the slow path (which re-derives everything from `func`),
        // while base and derived constructors both stay on the fast path — BranchIfBase only
        // selects how `this` is established.
        ValueVertex *superCtor = self->NewVertex<LoadPrototypeFromNonJSProxyObjectVertex>(currentBlock, {func});
        ValueVertex *undefined = self->undefinedValue_;

        // Monomorphic super-ctor speculation: the interpreter records the call slot only
        // after its own constructor checks, so a JSFunction in the slot is a constructor
        // by construction, and one identity deopt subsumes the heap-object and
        // JS-constructor guards below. A miss means func's [[Prototype]] changed after
        // profiling; the slot is rewritten on the next execution, so a recompile adapts.
        ValueVertex *expectedSuperCtor = nullptr;
        CallFeedback feedback = self->pgoContext_.ReadCallFeedback(bcInfo);
        if (feedback.IsMonomorphicJSFunction()) {
            expectedSuperCtor = GetHeapConstant(feedback.target);
        }

        BB *baseSplitBlock = self->NewBlock();
        BB *baseAdmissionBlock = self->NewBlock();
        BB *baseBlock = self->NewBlock();
        BB *derivedBlock = self->NewBlock();
        BB *fastBlock = self->NewBlock();
        BB *slowBlock = self->NewBlock();
        BB *mergeBlock = self->NewBlock();
        // Each guard failure edge needs its own trampoline block: a conditional
        // branch must not land directly on the multi-predecessor slow block
        // (a critical edge). Conditional edges carry no per-edge gap moves in
        // codegen, and the register allocator only initializes/merges a shared
        // target through unconditional jumps.

        if (expectedSuperCtor != nullptr) {
            BuildEagerDeoptIfReferenceNotEqual(superCtor, expectedSuperCtor);
            self->FinishBlockWithJump(currentBlock, baseSplitBlock);
        } else {
            BB *checkCtorBlock = self->NewBlock();
            BB *slowNotHeapObjectBlock = self->NewBlock();
            BB *slowNotJSConstructorBlock = self->NewBlock();
            // Super-ctor guards (fail fast into the slow block; superCtor stays untouched
            // for the slow path to re-examine): a heap object first — the constructor
            // JSFunction guard dereferences the hclass — then a constructor JSFunction.
            self->FinishBlockWithBranch<BranchIfTaggedHeapObjectVertex>(
                currentBlock, {superCtor}, checkCtorBlock, slowNotHeapObjectBlock);

            currentBlock = checkCtorBlock;
            self->FinishBlockWithBranch<BranchIfJSConstructorFunctionVertex>(
                currentBlock, {superCtor}, baseSplitBlock, slowNotJSConstructorBlock);

            for (BB *slowEntry : {slowNotHeapObjectBlock, slowNotJSConstructorBlock}) {
                currentBlock = slowEntry;
                slowEntry->SetDeferred(true);
                self->FinishBlockWithJump(slowEntry, slowBlock);
            }
        }

        currentBlock = baseSplitBlock;
        self->FinishBlockWithBranch<BranchIfBaseVertex>(
            currentBlock, {superCtor}, baseAdmissionBlock, derivedBlock);

        // Base-arm-only newTarget admission: newTarget must be a JSFunction whose
        // proto-or-hclass slot holds a JSHClass describing JS_OBJECT instances — exactly
        // the FastSuperAllocateThis stub's inline-path conditions, so the stub's
        // reentrant CallRuntime fallback (NewThisObject reads newTarget.prototype and
        // can run a proxy trap) is unreachable here. The derived arm skips this guard:
        // it forwards newTarget to the callee untouched. An undefined newTarget (derived
        // constructor invoked without new) never reaches the guard's dereference: class
        // constructor frames are only entered through [[Construct]].
        BB *slowNotReadyBlock = self->NewBlock();
        currentBlock = baseAdmissionBlock;
        self->FinishBlockWithBranch<BranchIfNewTargetHClassReadyVertex>(
            currentBlock, {newTarget}, baseBlock, slowNotReadyBlock);

        currentBlock = slowNotReadyBlock;
        slowNotReadyBlock->SetDeferred(true);
        self->FinishBlockWithJump(slowNotReadyBlock, slowBlock);

        currentBlock = baseBlock;
        auto *allocatedThis = self->NewVertex<FastSuperAllocateThisVertex>(
            compileInfoFacts_, currentBlock, {glue, superCtor, newTarget});
        // The base arm owns the only live undefined path (a base super's explicit
        // `return undefined`), so its result check falls back to allocatedThis and
        // the deopt bundle carries the bound-this spill slot as a pseudo vreg.
        ChunkVector<ValueVertex *> baseCallInputs(self->chunk_);
        baseCallInputs.push_back(superCtor);
        baseCallInputs.push_back(newTarget);
        baseCallInputs.push_back(allocatedThis);
        for (uint32_t idx = 0; idx < argc; idx++) {
            baseCallInputs.push_back(LoadRegister(bcInfo, static_cast<int>(idx)));
        }
        auto *baseCall = self->NewVertex<CallBaseConstructorVertex>(
            compileInfoFacts_, currentBlock, baseCallInputs, argc);
        UpdateCatchBlockData(baseCall);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, baseCall);
        self->FinishBlockWithJump(baseBlock, fastBlock);

        currentBlock = derivedBlock;
        // The derived arm passes an undefined placeholder this (the callee's own
        // super chain re-initializes it) and its result check only tests for an
        // object: the frontend guarantees a derived constructor never returns
        // undefined across the call boundary, so non-objects can only be raw
        // primitives and throw.
        ChunkVector<ValueVertex *> derivedCallInputs(self->chunk_);
        derivedCallInputs.push_back(superCtor);
        derivedCallInputs.push_back(newTarget);
        derivedCallInputs.push_back(undefined);
        for (uint32_t idx = 0; idx < argc; idx++) {
            derivedCallInputs.push_back(LoadRegister(bcInfo, static_cast<int>(idx)));
        }
        auto *derivedCall = self->NewVertex<CallDerivedConstructorVertex>(
            compileInfoFacts_, currentBlock, derivedCallInputs, argc);
        UpdateCatchBlockData(derivedCall);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, derivedCall);
        self->FinishBlockWithJump(derivedBlock, fastBlock);

        currentBlock = fastBlock;
        ValueVertex *fastResult = self->NewPhiVertexWith(currentBlock, {baseCall, derivedCall},
                                                         self->AccIndex());
        self->FinishBlockWithJump(fastBlock, mergeBlock);

        currentBlock = slowBlock;
        slowBlock->SetDeferred(true);
        ValueVertex *taggedArgc = TaggedConstantFromInt32(static_cast<int>(argc));
        ValueVertex *taggedArray = TaggedArrayFromValueIn(bcInfo, taggedArgc, argc);
        ValueVertex *slowResult = RuntimeCallWithLazyDeopt(
            {func, newTarget, taggedArray, taggedArgc}, RTSTUB_ID(OptSuperCall));
        self->FinishBlockWithJump(slowBlock, mergeBlock);

        currentBlock = mergeBlock;
        return self->NewPhiVertexWith(currentBlock, {fastResult, slowResult}, self->AccIndex());
    }

    void LowerSuperCallThisRange(const BytecodeInfo *bcInfo)
    {
        uint32_t inputSize = bcInfo->inputs.size();

        ValueVertex *thisFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *newTarget = LoadParam(NEW_TARGET_PARAM_INDEX);

        frameState.SetAcc(BuildSuperCallWithFastPath(bcInfo, thisFunc, newTarget, inputSize));
    }

    void LowerSuperCallArrowRange(const BytecodeInfo *bcInfo)
    {
        uint32_t argc = bcInfo->inputs.size();
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *newTarget = LoadParam(NEW_TARGET_PARAM_INDEX);
        ValueVertex *taggedArgc = TaggedConstantFromInt32(static_cast<int>(argc));
        ValueVertex *taggedArray = TaggedArrayFromValueIn(bcInfo, taggedArgc, argc);

        RuntimeCallToAccWithLazyDeopt({func, newTarget, taggedArray, taggedArgc}, RTSTUB_ID(OptSuperCall));
    }

    void LowerSuperCallSpread(const BytecodeInfo *bcInfo)
    {
        ValueVertex *array = LoadRegister(bcInfo, 0);
        ValueVertex *func = frameState.GetAcc();
        ValueVertex *newTarget = LoadParam(NEW_TARGET_PARAM_INDEX);

        ValueVertex *argsArray = CommonStubCall({glue, array, GlobalEnv()}, CommonStubID::GetCallSpreadArgs);
        RuntimeCallToAccWithLazyDeopt({func, newTarget, argsArray}, RTSTUB_ID(OptSuperCallSpread));
    }

    void LowerSuperCallForwardAllArgs(const BytecodeInfo *bcInfo)
    {
        ValueVertex *func = LoadRegister(bcInfo, 0);
        ValueVertex *superFunc = CommonStubCall({glue, func}, CommonStubID::GetPrototype);
        ValueVertex *newTarget = LoadParam(NEW_TARGET_PARAM_INDEX);
        ValueVertex *taggedActualArgc = TaggedActualArgc();

        RuntimeCallToAccWithLazyDeopt({superFunc, newTarget, taggedActualArgc}, RTSTUB_ID(OptSuperCallForwardAllArgs));
    }

    void LowerNewObjApply(const BytecodeInfo *bcInfo)
    {
        ValueVertex *target = LoadRegister(bcInfo, 0);
        ValueVertex *args = frameState.GetAcc();
        RuntimeCallToAccWithLazyDeopt({target, args}, RTSTUB_ID(NewObjApply));
    }

    void LowerNewObjRange(const BytecodeInfo *bcInfo)
    {
        uint32_t inputSize = bcInfo->inputs.size();
        ASSERT(inputSize > 0);
        ChunkVector<ValueVertex *> args(self->chunk_);
        for (uint32_t idx = 0; idx < inputSize; idx++) {
            args.push_back(LoadRegister(bcInfo, idx));
        }
        uint32_t actualArgc = inputSize - 1U;
        std::optional<BuiltinConstructPlan> builtinPlan = TryCreateBuiltinLoadConstructPlan(args.front(), actualArgc);
        ConstructorFeedback feedback = self->pgoContext_.ReadConstructorFeedback(bcInfo);
        if (!builtinPlan.has_value()) {
            builtinPlan = TryCreateProfiledKnownBuiltinConstructPlan(feedback, actualArgc);
        }
        if (builtinPlan.has_value()) {
            ValueVertex *result = BuildKnownBuiltinConstruct(args, builtinPlan.value());
            frameState.SetAcc(result);
            return;
        }
        if (self->IsLazyDeoptEnabled()) {
            auto plan = TryCreateProfiledKnownConstructPlan(feedback);
            if (plan.has_value()) {
                frameState.SetAcc(BuildKnownNewObjRange(args, args.front(), plan.value()));
                return;
            }
            frameState.SetAcc(BuildDynamicNewObjRange(args, args.front()));
            return;
        }
        RuntimeCallToAccWithLazyDeopt(args, RTSTUB_ID(OptNewObjRange));
    }

    // -------- Category #9: Object/Array Creation --------

    // Builds the shared fast/slow allocation structure for constant and runtime
    // sizes: check configuration and bump capacity, bump-allocate on the
    // fast path or call the runtime allocator on the deferred slow path, then
    // merge both raw addresses in a Phi for the following initializer.
    ValueVertex* AllocateInYoung(ValueVertex* size, ValueVertex* hclass)
    {
        ASSERT(size->GetValueRepresentation() == ValueRepresentation::INT64);
        ValueVertex* taggedSize;
        if (auto* constant = size->TryCast<Int64ConstantVertex>()) {
            taggedSize = self->graph_->GetTaggedConstant(static_cast<uint64_t>(constant->GetValue())
                                                         | JSTaggedValue::TAG_INT);
        } else {
            auto* tag = self->graph_->GetInt64Constant(static_cast<int64_t>(JSTaggedValue::TAG_INT));
            auto* bits = self->NewVertex<I64BitwiseBinaryVertex>(
                compileInfoFacts_, currentBlock, {size, tag}, IntBitwiseKind::BITWISE_OR);
            taggedSize = self->NewVertex<RawI64ToTaggedVertex>(compileInfoFacts_, currentBlock, {bits});
        }
        // Encode the full size before the capacity check, keeping both allocation
        // edges and the raw-storage initialization interval free of conversions.
        CompileInfoFacts* entryFacts = compileInfoFacts_;
        BB* fastBlock = self->NewBlock();
        BB* slowBlock = self->NewBlock();
        BB* doneBlock = self->NewBlock();
        slowBlock->SetDeferred(true);
        auto* canAllocate
            = self->NewVertex<CanAllocateInYoungVertex>(compileInfoFacts_, currentBlock, {glue, size});
        self->FinishBlockWithBranch<BranchIfTrueVertex>(currentBlock, {canAllocate}, fastBlock, slowBlock);

        currentBlock = fastBlock;
        compileInfoFacts_ = entryFacts->Clone();
        auto* fastAllocation
            = self->NewVertex<AllocateInYoungVertex>(compileInfoFacts_, currentBlock, {glue, size});
        self->FinishBlockWithJump(currentBlock, doneBlock);
        CompileInfoFacts* fastFacts = compileInfoFacts_;

        currentBlock = slowBlock;
        compileInfoFacts_ = entryFacts->Clone();
        // The HClass alone does not complete initialization: attach no catch or lazy-deopt state.
        auto* slowAllocation = self->NewVertex<CallRuntimeVertex>(
            compileInfoFacts_, currentBlock, {taggedSize, hclass}, RTSTUB_ID(AllocateInYoung));
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = doneBlock;
        compileInfoFacts_->Merge(*fastFacts);
        return self->NewPhiVertexWith(currentBlock, {fastAllocation, slowAllocation}, self->AccIndex());
    }

    ValueVertex *BuildEmptyObject()
    {
        uint32_t objectSize = 0;
        ValueVertex* hclass = nullptr;
        {
            auto *broker = self->pgoContext_.GetBroker();
            ArkSteedHClassRef hclassRef;
            if (broker->TryGetEmptyObjectLayout(&hclassRef, &objectSize)) {
                hclass = GetHeapConstant(hclassRef);
            }
        }
        if (hclass == nullptr) {
            auto* result = RuntimeCallWithLazyDeopt({}, RTSTUB_ID(CreateEmptyObject));
            compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::OTHER_JS_RECEIVER);
            return result;
        }
        ValueVertex* size = self->graph_->GetInt64Constant(static_cast<int64_t>(objectSize));
        auto* allocation = AllocateInYoung(size, hclass);
        auto* object = self->NewVertex<InitializeEmptyObjectVertex>(
            compileInfoFacts_, currentBlock, {allocation, glue, hclass, size}, objectSize);
        compileInfoFacts_->EnsureType(object, NodeInfo::NodeType::OTHER_JS_RECEIVER);
        return object;
    }

    void LowerCreateEmptyObject()
    {
        frameState.SetAcc(BuildEmptyObject());
    }

    void ApplyArrayCreationHClass(ValueVertex *array, uint32_t slotId)
    {
        auto *broker = self->pgoContext_.GetBroker();
        ArkSteedHClassRef hclassRef;
        if (!broker->TryGetArrayCreationHClass(slotId, &hclassRef)) {
            return;
        }
        JSHClass *hclass = nullptr;
        ValueVertex *constant = GetHeapConstant(hclassRef);
        if (!TryResolveHClassRef(hclassRef, &hclass) || !hclass->IsJSArray() || constant == nullptr) {
            return;
        }
        self->NewVertex<TransitionHClassWithBarrierVertex>(compileInfoFacts_, currentBlock, {glue, array, constant});
        compileInfoFacts_->RecordHClass(array, hclass, false);
    }

    void LowerCreateEmptyArray(const BytecodeInfo *bcInfo)
    {
        uint32_t objectSize = 0;
        ValueVertex* globalEnv = nullptr;
        {
            auto *broker = self->pgoContext_.GetBroker();
            ArkSteedObjectRef envRef;
            if (broker->TryGetEmptyArrayLayout(&envRef, &objectSize)) {
                globalEnv = GetHeapConstant(envRef);
            }
        }
        ValueVertex* array;
        if (globalEnv == nullptr) {
            array = CommonStubCallToAccWithLazyDeopt({glue, GlobalEnv()}, CommonStubID::CreateEmptyArray);
        } else {
            constexpr int32_t arrayFunctionOffset
                = GlobalEnv::HEADER_SIZE + GlobalEnv::ARRAY_FUNCTION_INDEX * JSTaggedValue::TaggedTypeSize();
            auto* arrayFunction = self->NewVertex<LoadTaggedFieldVertex>(
                compileInfoFacts_, currentBlock, {globalEnv}, arrayFunctionOffset);
            auto* hclass = self->NewVertex<LoadTaggedFieldVertex>(
                compileInfoFacts_, currentBlock, {arrayFunction}, JSFunction::PROTO_OR_DYNCLASS_OFFSET);
            ValueVertex* size = self->graph_->GetInt64Constant(static_cast<int64_t>(objectSize));
            auto* allocation = AllocateInYoung(size, hclass);
            array = self->NewVertex<InitializeEmptyArrayVertex>(
                compileInfoFacts_, currentBlock, {allocation, glue, globalEnv, hclass}, objectSize);
        }
        frameState.SetAcc(array);
        compileInfoFacts_->EnsureType(array, NodeInfo::NodeType::JS_ARRAY);
        ApplyArrayCreationHClass(array, static_cast<uint32_t>(bcInfo->slotId.GetId()));
        frameState.SetAcc(array);
    }

    void LowerCreateObjectWithBuffer(const BytecodeInfo *bcInfo)
    {
        uint16_t cpIdx = GetConstDataId<uint16_t>(bcInfo, 0);
        ValueVertex* literal = nullptr;
        CreateObjectWithBufferInfo info;
        ArkSteedHeapBroker* broker = self->pgoContext_.GetBroker();
        // These GC modes require the complete creation stub, including its initialization barriers.
        if (!g_isEnableCMCGC && !G_USE_CMS_GC && broker != nullptr) {
            ArkSteedObjectRef literalRef;
            if (broker->TryGetObjectLiteralFromConstantPool(cpIdx, self->chunk_, &literalRef, &info)) {
                literal = GetHeapConstant(literalRef);
            }
        }
        if (literal == nullptr) {
            ValueVertex* index = self->graph_->GetInt32Constant(cpIdx);
            ValueVertex* obj = ObjectFromConstPool(index);
            ValueVertex* lexEnv = LoadRegister(bcInfo, 1);
            ValueVertex* object = CommonStubCallToAccWithLazyDeopt(
                {glue, obj, lexEnv}, CommonStubID::CreateObjectHavingMethod, SideEffectKind::SAFE_CALL);
            compileInfoFacts_->EnsureType(object, NodeInfo::NodeType::OTHER_JS_RECEIVER);
            return;
        }

        auto* hclass = self->NewVertex<LoadTaggedFieldVertex>(
            compileInfoFacts_, currentBlock, {literal}, TaggedObject::HCLASS_OFFSET);
        ValueVertex* size = self->graph_->GetInt64Constant(static_cast<int64_t>(info.objectSize));
        auto* allocation = AllocateInYoung(size, hclass);
        auto* object = self->NewVertex<InitializeObjectWithBufferVertex>(
            compileInfoFacts_, currentBlock, {allocation, glue, literal, hclass}, info);
        frameState.SetAcc(object);
        compileInfoFacts_->EnsureType(object, NodeInfo::NodeType::OTHER_JS_RECEIVER);
    }

    void LowerCreateObjectWithExcludedKeys(const BytecodeInfo *bcInfo)
    {
        uint32_t inputSize = bcInfo->inputs.size();
        ChunkVector<ValueVertex *> args(self->chunk_);
        for (uint32_t idx = 0; idx < inputSize; idx++) {
            args.push_back(LoadRegister(bcInfo, idx));
        }
        RuntimeCallToAccWithLazyDeopt(args, RTSTUB_ID(OptCreateObjectWithExcludedKeys));
    }

    void LowerCreateArrayWithBuffer(const BytecodeInfo *bcInfo)
    {
        uint16_t cpIdx = GetConstDataId<uint16_t>(bcInfo, 0);
        uint32_t rawSlotId = GetICSlotId<uint32_t>(bcInfo, 1);
        uint32_t objectSize = 0;
        ValueVertex* literal = nullptr;
        ArkSteedHeapBroker* broker = self->pgoContext_.GetBroker();
        // These GC modes require the complete creation stub, including its initialization barriers.
        if (!g_isEnableCMCGC && !G_USE_CMS_GC && broker != nullptr) {
            ArkSteedObjectRef literalRef;
            if (broker->TryGetArrayLiteralFromConstantPool(cpIdx, &literalRef, &objectSize)) {
                literal = GetHeapConstant(literalRef);
            }
        }
        ValueVertex* array;
        if (literal == nullptr) {
            ValueVertex* index = self->graph_->GetInt32Constant(cpIdx);
            ValueVertex* jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
            ValueVertex* slotId = self->graph_->GetInt32Constant(static_cast<int32_t>(rawSlotId));
            array = CommonStubCallToAccWithLazyDeopt({glue, index, jsFunc, slotId, GlobalEnv()},
                                                     CommonStubID::CreateArrayWithBuffer);
        } else {
            auto* hclass = self->NewVertex<LoadTaggedFieldVertex>(
                compileInfoFacts_, currentBlock, {literal}, TaggedObject::HCLASS_OFFSET);
            ValueVertex* size = self->graph_->GetInt64Constant(static_cast<int64_t>(objectSize));
            auto* allocation = AllocateInYoung(size, hclass);
            array = self->NewVertex<InitializeArrayWithBufferVertex>(
                compileInfoFacts_, currentBlock, {allocation, glue, literal, hclass}, objectSize);
            frameState.SetAcc(array);
            compileInfoFacts_->EnsureType(array, NodeInfo::NodeType::JS_ARRAY);
        }

        // Creation feedback can widen without migrating the literal's numeric backing store.
        // Preserve the actual HClass on both paths until element migration is supported here.
        if (self->preproc_->GetEnv()->GetJSOptions().IsEnableMutantArray()) {
            return;
        }

        ArkSteedHClassRef creatorHClassRef;
        if (!broker->TryGetArrayCreationHClass(rawSlotId, &creatorHClassRef)) {
            return;
        }
        JSHClass *creatorHClass = nullptr;
        ValueVertex *creatorHClassConstant = GetHeapConstant(creatorHClassRef);
        if (!TryResolveHClassRef(creatorHClassRef, &creatorHClass) || creatorHClass == nullptr ||
            !creatorHClass->IsJSArray() || creatorHClassConstant == nullptr) {
            return;
        }
        self->NewVertex<TransitionHClassWithBarrierVertex>(
            compileInfoFacts_, currentBlock, {glue, array, creatorHClassConstant});
        compileInfoFacts_->RecordHClass(array, creatorHClass, false);
    }

    void LowerCreateRegExpWithLiteral(const BytecodeInfo *bcInfo)
    {
        ValueVertex *stringId = self->graph_->GetInt32Constant(GetConstDataId<int>(bcInfo, 0));
        ValueVertex *pattern = StringFromConstPool(stringId);
        ValueVertex *flags = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 1));

        RuntimeCallToAccWithLazyDeopt({pattern, flags}, RTSTUB_ID(CreateRegExpWithLiteral));
    }

    void LowerCreateIterResultObj(const BytecodeInfo *bcInfo)
    {
        ValueVertex *value = LoadRegister(bcInfo, 0);
        ValueVertex *done = LoadRegister(bcInfo, 1);
        RuntimeCallToAccWithLazyDeopt({value, done}, RTSTUB_ID(CreateIterResultObj));
    }

    // -------- Category #10: Class/Function/Field Definition --------

    void LowerDefineMethod(const BytecodeInfo *bcInfo)
    {
        ValueVertex *taggedMethodId = TaggedConstantFromInt32(GetConstDataId<int>(bcInfo, 0));
        ValueVertex *length = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 1));
        ValueVertex *env = LoadRegister(bcInfo, 2);  // 2: env register index
        ValueVertex *homeObject = frameState.GetAcc();
        ValueVertex *module = ModuleFromFunction();
        ValueVertex *method = MethodFromConstPool(taggedMethodId);

#if ECMASCRIPT_ENABLE_IC
        // 3 : slotId operand index
        ValueVertex *slotId = TaggedConstantFromInt32(GetICSlotId<int>(bcInfo, 3));
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);

        RuntimeCallToAccWithLazyDeopt({method, homeObject, length, env, module, slotId, jsFunc},
                                      RTSTUB_ID(DefineMethod));
#else
        RuntimeCallToAcc({method, homeObject, length, env, module}, RTSTUB_ID(DefineMethod));
#endif
    }

    void LowerDefineFunc(const BytecodeInfo *bcInfo)
    {
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *slotId = self->graph_->GetInt32Constant(GetICSlotId<int>(bcInfo, 0));
        ValueVertex *methodId = self->graph_->GetInt32Constant(GetConstDataId<int>(bcInfo, 1));
        // 2: length operand index
        ValueVertex *length = self->graph_->GetInt32Constant(GetImmediate<int>(bcInfo, 2));
        // 3: lexicalEnv register index
        ValueVertex *lexicalEnv = LoadRegister(bcInfo, 3);

        CommonStubCallToAccWithLazyDeopt({glue, jsFunc, methodId, length, lexicalEnv, slotId, GlobalEnv()},
                                         CommonStubID::Definefunc);
    }

    void LowerDefineClassWithBuffer(const BytecodeInfo *bcInfo)
    {
        ValueVertex *methodId = TaggedConstantFromInt32(GetConstDataId<int>(bcInfo, 0));
        ValueVertex *literalId = TaggedConstantFromInt32(GetConstDataId<int>(bcInfo, 1));
        ValueVertex *length = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 2));
        ValueVertex *proto = LoadRegister(bcInfo, 3);       // 3: proto register index
        ValueVertex *lexicalEnv = LoadRegister(bcInfo, 4);  // 4: lexicalEnv register index
        ValueVertex *sharedConstPool = SharedConstPool();
        ValueVertex *module = ModuleFromFunction();

#if ECMASCRIPT_ENABLE_IC
        ValueVertex *slotId = TaggedConstantFromInt32(GetICSlotId<int>(bcInfo, 5));  // 5 : Slot ID index
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);

        RuntimeCallToAccWithLazyDeopt(
            {proto, lexicalEnv, sharedConstPool, methodId, literalId, module, length, slotId, jsFunc},
            RTSTUB_ID(CreateClassWithBuffer));
#else
        RuntimeCallToAcc({proto, lexicalEnv, sharedConstPool, methodId, literalId, module, length},
                         RTSTUB_ID(CreateClassWithBuffer));
#endif
    }

    void LowerDefineGetterSetterByValue(const BytecodeInfo *bcInfo)
    {
        ValueVertex *obj = LoadRegister(bcInfo, 0);
        ValueVertex *prop = LoadRegister(bcInfo, 1);
        ValueVertex *getter = LoadRegister(bcInfo, 2);  // 2: getter register index
        ValueVertex *setter = LoadRegister(bcInfo, 3);  // 3: setter register index
        ValueVertex *acc = frameState.GetAcc();
        ValueVertex *undefinedValue = self->undefinedValue_;
        ValueVertex *taggedOne = TaggedConstantFromInt32(1);

        RuntimeCallToAccWithLazyDeopt({obj, prop, getter, setter, acc, undefinedValue, taggedOne},
                                      RTSTUB_ID(DefineGetterSetterByValue));
    }

    void LowerDefinePropertyByName(const BytecodeInfo *bcInfo, uint32_t bcIndex)
    {
        ValueVertex *receiver = LoadRegister(bcInfo, 2);  // 2: obj register index
        uint16_t constDataId = GetConstDataId(bcInfo, 1);
        ValueVertex *value = frameState.GetAcc();

        NamedStoreAccessSet access;
        auto factory = self->pgoContext_.CreateAccessInfoFactory(bcInfo);
        if (factory.TryBuildNamedStoreAccessInfo(0, &access) && access.caseCount > 0) {
            for (uint32_t i = 0; i < access.caseCount; ++i) {
                ASSERT(access.cases[i].kind == AccessKind::TRANSITION);
                ASSERT(!access.cases[i].isSharedStore);
            }
            if (TryLowerNamedStoreAccessSet(bcIndex, access, receiver, value, PropertyKey::ConstDataId(constDataId))) {
                return;
            }
        }
        ValueVertex *stringId = self->graph_->GetInt32Constant(static_cast<int>(constDataId));
        ValueVertex *prop = StringFromConstPool(stringId);
        CommonStubCallWithLazyDeopt({glue, receiver, prop, value, GlobalEnv()}, CommonStubID::DefineField);
    }

    // Note: Legacy bytecode only. Zero production by current ArkTS frontend (es2abc).
    void LowerDefineFieldByName(const BytecodeInfo *bcInfo)
    {
        ValueVertex *stringId = self->graph_->GetInt32Constant(GetConstDataId<int>(bcInfo, 1));
        ValueVertex *prop = StringFromConstPool(stringId);
        ValueVertex *obj = LoadRegister(bcInfo, 2);  // 2: obj register index
        ValueVertex *value = frameState.GetAcc();
        CommonStubCallWithLazyDeopt({glue, obj, prop, value, GlobalEnv()}, CommonStubID::DefineField);
    }

    void LowerDefineFieldByValue(const BytecodeInfo *bcInfo)
    {
        ValueVertex *receiver = LoadRegister(bcInfo, 1);
        ValueVertex *propKey = LoadRegister(bcInfo, 0);
        ValueVertex *acc = frameState.GetAcc();
        CommonStubCallWithLazyDeopt({glue, receiver, propKey, acc, GlobalEnv()}, CommonStubID::DefineField);
    }

    void LowerDefineFieldByIndex(const BytecodeInfo *bcInfo)
    {
        ValueVertex *receiver = LoadRegister(bcInfo, 1);
        ValueVertex *propKey = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 0));
        ValueVertex *acc = frameState.GetAcc();
        CommonStubCallWithLazyDeopt({glue, receiver, propKey, acc, GlobalEnv()}, CommonStubID::DefineField);
    }

    void LowerCreatePrivateProperty(const BytecodeInfo *bcInfo)
    {
        ValueVertex *count = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 0));
        ValueVertex *literalId = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 1));
        ValueVertex *lexicalEnv = LoadRegister(bcInfo, 2);  // 2: lexicalEnv register index
        ValueVertex *constpool = SharedConstPool();
        ValueVertex *module = ModuleFromFunction();

        RuntimeCallWithLazyDeopt({lexicalEnv, count, constpool, literalId, module}, RTSTUB_ID(CreatePrivateProperty));
    }

    void LowerDefinePrivateProperty(const BytecodeInfo *bcInfo)
    {
        ValueVertex *levelIndex = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 0));
        ValueVertex *slotIndex = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 1));
        ValueVertex *obj = LoadRegister(bcInfo, 2);         // 2: obj register index
        ValueVertex *lexicalEnv = LoadRegister(bcInfo, 3);  // 3: lexicalEnv register index
        ValueVertex *value = frameState.GetAcc();
        RuntimeCallWithLazyDeopt({lexicalEnv, levelIndex, slotIndex, obj, value}, RTSTUB_ID(DefinePrivateProperty));
    }

    // -------- Category #11: Iterators --------

    void LowerGetIterator()
    {
        ValueVertex *obj = frameState.GetAcc();
        CommonStubCallToAccWithLazyDeopt({glue, obj, GlobalEnv()}, CommonStubID::GetIterator);
    }

    void LowerGetPropIterator()
    {
        ValueVertex *object = frameState.GetAcc();
        CommonStubCallToAccWithLazyDeopt({glue, object, GlobalEnv()}, CommonStubID::Getpropiterator);
    }

    void LowerCloseIterator(const BytecodeInfo *bcInfo)
    {
        ValueVertex *iterator = LoadRegister(bcInfo, 0);
        RuntimeCallToAccWithLazyDeopt({iterator}, RTSTUB_ID(CloseIterator));
    }

    void LowerGetNextPropName(const BytecodeInfo *bcInfo)
    {
        ValueVertex *iterator = LoadRegister(bcInfo, 0);
        RuntimeCallToAccWithLazyDeopt({iterator}, RTSTUB_ID(GetNextPropNameSlowpath));
    }

    // -------- Category #12: Lexical Environment --------

    int32_t GetLexicalEnvSlotOffset(uint16_t slot) const
    {
        return static_cast<int32_t>(TaggedArray::DATA_OFFSET +
                                    (LexicalEnv::RESERVED_ENV_LENGTH + slot) * JSTaggedValue::TaggedTypeSize());
    }

    int32_t GetLexicalEnvParentOffset() const
    {
        return static_cast<int32_t>(TaggedArray::DATA_OFFSET +
                                    LexicalEnv::PARENT_ENV_INDEX * JSTaggedValue::TaggedTypeSize());
    }

    ValueVertex *BuildEnvSlotLoad(ValueVertex *env, int32_t offset)
    {
        ASSERT(env != nullptr);
        bool isConstantField = IsEnvConstantFieldOffset(offset);
        ValueVertex *cached = nullptr;
        if (isConstantField) {
            cached = compileInfoFacts_->LookupEnvConstant(env, offset);
        } else {
            cached = compileInfoFacts_->LookupEnvSlot(env, offset);
        }
        if (cached != nullptr) {
            return cached;
        }

        ValueVertex *value = self->NewVertex<LoadTaggedFieldVertex>(currentBlock, {env}, offset);
        if (isConstantField) {
            compileInfoFacts_->RecordEnvConstant(env, offset, value);
        } else {
            compileInfoFacts_->RecordEnvSlot(env, offset, value);
        }
        return value;
    }

    ValueVertex *BuildLexicalEnvAtLevel(ValueVertex *baseEnv, uint16_t level)
    {
        ValueVertex *env = baseEnv;
        for (uint16_t i = 0; i < level; ++i) {
            env = BuildEnvSlotLoad(env, GetLexicalEnvParentOffset());
        }
        return env;
    }

    void LowerNewLexicalEnv(const BytecodeInfo *bcInfo)
    {
        ValueVertex *parent = LoadRegister(bcInfo, 1);
        ValueVertex *numVars = self->graph_->GetInt32Constant(GetImmediate<int>(bcInfo, 0));
        ValueVertex *newEnv =
            CommonStubCall({glue, parent, numVars}, CommonStubID::NewLexicalEnv, SideEffectKind::SAFE_CALL);

        frameState.SetAcc(newEnv);
        frameState.SetLexicalEnv(newEnv);
        compileInfoFacts_->RecordEnvConstant(newEnv, GetLexicalEnvParentOffset(), parent);
    }

    void LowerNewLexicalEnvWithName(const BytecodeInfo *bcInfo)
    {
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *level = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 0));
        ValueVertex *slotId = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 1));
        ValueVertex *parent = LoadRegister(bcInfo, 2);  // 2: env register index
        ValueVertex *newEnv = RuntimeCall({level, slotId, parent, jsFunc}, RTSTUB_ID(OptNewLexicalEnvWithName),
                                          SideEffectKind::SAFE_CALL);

        frameState.SetAcc(newEnv);
        frameState.SetLexicalEnv(newEnv);
        compileInfoFacts_->RecordEnvConstant(newEnv, GetLexicalEnvParentOffset(), parent);
    }

    void LowerPopLexicalEnv(const BytecodeInfo *bcInfo)
    {
        ValueVertex *currentEnv = LoadRegister(bcInfo, 0);
        ValueVertex *parentEnv = BuildEnvSlotLoad(currentEnv, GetLexicalEnvParentOffset());

        // POPLEXENV only updates the lexical env; it must not overwrite the accumulator.
        frameState.SetLexicalEnv(parentEnv);
        compileInfoFacts_->ClearEnvSlotsFor(currentEnv);
    }

    void LowerLdLexVar(const BytecodeInfo *bcInfo)
    {
        uint16_t level = GetImmediate<uint16_t>(bcInfo, 0);
        uint16_t slot = GetImmediate<uint16_t>(bcInfo, 1);
        ValueVertex *lexicalEnv = LoadRegister(bcInfo, 2);  // 2: lexicalEnv register index
        ValueVertex *targetEnv = BuildLexicalEnvAtLevel(lexicalEnv, level);
        frameState.SetAcc(BuildEnvSlotLoad(targetEnv, GetLexicalEnvSlotOffset(slot)));
    }

    void LowerStLexVar(const BytecodeInfo *bcInfo)
    {
        uint16_t level = GetImmediate<uint16_t>(bcInfo, 0);
        uint16_t slot = GetImmediate<uint16_t>(bcInfo, 1);
        ValueVertex *lexicalEnv = LoadRegister(bcInfo, 2);  // 2: lexicalEnv register index
        ValueVertex *value = frameState.GetAcc();
        ASSERT(value != nullptr);
        ValueVertex *targetEnv = BuildLexicalEnvAtLevel(lexicalEnv, level);
        int32_t offset = GetLexicalEnvSlotOffset(slot);
        self->NewVertex<StoreEnvSlotVertex>(compileInfoFacts_, currentBlock, {targetEnv, value}, offset);
        if (ClassifyDirectWriteBarrierValueKind(value) != ArkSteedWriteBarrierValueKind::NonHeap) {
            self->NewVertex<SetValueWithBarrierVertex>(compileInfoFacts_, currentBlock, {glue, targetEnv, value},
                                                       offset);
        }
        compileInfoFacts_->RecordEnvSlot(targetEnv, offset, value);
    }

    // -------- Category #13: Modules --------

    void LowerLdExternalModuleVar(const BytecodeInfo *bcInfo)
    {
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        int32_t index = GetImmediate<int32_t>(bcInfo, 0);
        if (TryBuildLoadExternalModuleVar(jsFunc, index)) {
            return;
        }
        ValueVertex *taggedIndex = TaggedConstantFromInt32(index);
        RuntimeCallToAccWithLazyDeopt({taggedIndex, jsFunc}, RTSTUB_ID(LdExternalModuleVarByIndexOnJSFunc));
    }

    void LowerLdLazyExternalModuleVar(const BytecodeInfo *bcInfo)
    {
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        int32_t index = GetImmediate<int32_t>(bcInfo, 0);
        ValueVertex *taggedIndex = TaggedConstantFromInt32(index);
        RuntimeCallToAccWithLazyDeopt({taggedIndex, jsFunc}, RTSTUB_ID(LdLazyExternalModuleVarByIndex));
    }

    void LowerGetModuleNamespace(const BytecodeInfo *bcInfo)
    {
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *index = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 0));
        RuntimeCallToAccWithLazyDeopt({index, jsFunc}, RTSTUB_ID(GetModuleNamespaceByIndexOnJSFunc));
    }

    void LowerLdLocalModuleVar(const BytecodeInfo *bcInfo)
    {
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *index = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 0));
        RuntimeCallToAccWithLazyDeopt({index, jsFunc}, RTSTUB_ID(LdLocalModuleVarByIndexOnJSFunc));
    }

    void LowerStModuleVar(const BytecodeInfo *bcInfo)
    {
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *index = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 0));
        ValueVertex *value = frameState.GetAcc();
        RuntimeCallWithLazyDeopt({index, value, jsFunc}, RTSTUB_ID(StModuleVarByIndexOnJSFunc));
    }

    void LowerDynamicImport()
    {
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *specifier = frameState.GetAcc();
        RuntimeCallToAccWithLazyDeopt({specifier, jsFunc}, RTSTUB_ID(DynamicImport));
    }

    void LowerLdPatchVar(const BytecodeInfo *bcInfo)
    {
        ValueVertex *index = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 0));
        RuntimeCallToAccWithLazyDeopt({index}, RTSTUB_ID(LdPatchVar));
    }

    void LowerStPatchVar(const BytecodeInfo *bcInfo)
    {
        ValueVertex *index = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 0));
        ValueVertex *value = frameState.GetAcc();
        RuntimeCallWithLazyDeopt({index, value}, RTSTUB_ID(StPatchVar));
    }

    // -------- Category #14: Miscellaneous --------

    enum class TypeOfKind : uint8_t {
        UNDEFINED,
        OBJECT,
        NUMBER,
        BOOLEAN,
        STRING,
        SYMBOL,
        FUNCTION,
        BIGINT,
        NATIVE_MODULE_FAILURE_INFO,
        UNKNOWN,
    };

    static TypeOfKind TypeOfKindFromObjectType(JSType type, bool callable)
    {
        if (type >= JSType::STRING_FIRST && type <= JSType::STRING_LAST) {
            return TypeOfKind::STRING;
        }
        if (type == JSType::SYMBOL) {
            return TypeOfKind::SYMBOL;
        }
        if (callable) {
            return TypeOfKind::FUNCTION;
        }
        if (type == JSType::BIGINT) {
            return TypeOfKind::BIGINT;
        }
        if (type == JSType::NATIVE_MODULE_FAILURE_INFO) {
            return TypeOfKind::NATIVE_MODULE_FAILURE_INFO;
        }
        return TypeOfKind::OBJECT;
    }

    static TypeOfKind TypeOfKindFromHClass(const JSHClass *hclass)
    {
        return hclass == nullptr ? TypeOfKind::UNKNOWN
                                 : TypeOfKindFromObjectType(hclass->GetObjectType(), hclass->IsCallable());
    }

    static TypeOfKind TypeOfKindFromConstant(JSTaggedValue value)
    {
        if (value.IsUndefined()) {
            return TypeOfKind::UNDEFINED;
        }
        if (value.IsNull()) {
            return TypeOfKind::OBJECT;
        }
        if (value.IsInt() || value.IsDouble()) {
            return TypeOfKind::NUMBER;
        }
        if (value.IsTrue() || value.IsFalse()) {
            return TypeOfKind::BOOLEAN;
        }
        if (value.IsHole()) {
            return TypeOfKind::UNKNOWN;
        }
        // Managed constants use HeapConstantVertex, not embedded raw addresses.
        ASSERT(!value.IsHeapObject());
        return TypeOfKind::UNKNOWN;
    }

    static TypeOfKind TypeOfKindFromKnownType(NodeInfo::NodeType knownType)
    {
        if (NodeInfo::IsEmptyNodeType(knownType) || knownType == NodeInfo::NodeType::UNKNOWN) {
            return TypeOfKind::UNKNOWN;
        }
        if (NodeInfo::NodeTypeIs(knownType, NodeInfo::NodeType::UNDEFINED)) {
            return TypeOfKind::UNDEFINED;
        }
        if (NodeInfo::NodeTypeIs(knownType, NodeInfo::NodeType::NULL_TYPE)) {
            return TypeOfKind::OBJECT;
        }
        if (NodeInfo::NodeTypeIs(knownType, NodeInfo::NodeType::NUMBER)) {
            return TypeOfKind::NUMBER;
        }
        if (NodeInfo::NodeTypeIs(knownType, NodeInfo::NodeType::BOOLEAN)) {
            return TypeOfKind::BOOLEAN;
        }
        if (NodeInfo::NodeTypeIs(knownType, NodeInfo::NodeType::STRING)) {
            return TypeOfKind::STRING;
        }
        if (NodeInfo::NodeTypeIs(knownType, NodeInfo::NodeType::SYMBOL)) {
            return TypeOfKind::SYMBOL;
        }
        if (NodeInfo::NodeTypeIs(knownType, NodeInfo::NodeType::JS_FUNCTION)) {
            return TypeOfKind::FUNCTION;
        }
        if (NodeInfo::NodeTypeIs(knownType, NodeInfo::NodeType::BIGINT)) {
            return TypeOfKind::BIGINT;
        }
        if (NodeInfo::NodeTypeIs(
                knownType, NodeInfo::UnionNodeType(NodeInfo::NodeType::JS_ARRAY, NodeInfo::NodeType::JS_TYPED_ARRAY))) {
            return TypeOfKind::OBJECT;
        }
        return TypeOfKind::UNKNOWN;
    }

    TypeOfKind TypeOfKindFromPossibleHClasses(ValueVertex *value) const
    {
        std::optional<NodeInfo::PossibleHClasses> hclasses = compileInfoFacts_->TryGetPossibleHClasses(value);
        if (!hclasses.has_value() || hclasses->empty()) {
            return TypeOfKind::UNKNOWN;
        }
        TypeOfKind kind = TypeOfKindFromHClass(hclasses->front());
        bool allAgree = std::all_of(hclasses->begin(), hclasses->end(),
                                    [kind](const JSHClass *hclass) { return TypeOfKindFromHClass(hclass) == kind; });
        return allAgree ? kind : TypeOfKind::UNKNOWN;
    }

    TypeOfKind ClassifyTypeOf(ValueVertex *value) const
    {
        if (auto *constant = value->TryCast<TaggedConstantVertex>()) {
            return TypeOfKindFromConstant(JSTaggedValue(constant->GetValue()));
        }
        if (std::optional<ArkSteedHeapRef> constantRef = TryGetConstantHeapRef(value)) {
            ArkSteedHeapBroker *broker = self->pgoContext_.GetBroker();
            if (broker != nullptr) {
                JSType type;
                bool callable = false;
                if (broker->TryGetObjectType(*constantRef, &type, &callable)) {
                        return TypeOfKindFromObjectType(type, callable);
                }
            }
        }
        TypeOfKind kind = TypeOfKindFromKnownType(compileInfoFacts_->GetKnownType(value));
        if (kind != TypeOfKind::UNKNOWN) {
            return kind;
        }
        return TypeOfKindFromPossibleHClasses(value);
    }

    ValueVertex *StringFromGlobalConstant(ConstantIndex index)
    {
        ValueVertex *constant = nullptr;
        ArkSteedHeapBroker *broker = self->pgoContext_.GetBroker();
        if (broker != nullptr) {
            ArkSteedNameRef stringRef;
            if (broker->TryGetGlobalConstantRef(index, &stringRef)) {
                constant = GetHeapConstant(stringRef);
            }
        }
        ASSERT(constant != nullptr);
        return constant;
    }

    static ConstantIndex StringIndexOfTypeOfKind(TypeOfKind kind)
    {
        switch (kind) {
            case TypeOfKind::UNDEFINED:
                return ConstantIndex::UNDEFINED_STRING_INDEX;
            case TypeOfKind::OBJECT:
                return ConstantIndex::OBJECT_STRING_INDEX;
            case TypeOfKind::NUMBER:
                return ConstantIndex::NUMBER_STRING_INDEX;
            case TypeOfKind::BOOLEAN:
                return ConstantIndex::BOOLEAN_STRING_INDEX;
            case TypeOfKind::STRING:
                return ConstantIndex::STRING_STRING_INDEX;
            case TypeOfKind::SYMBOL:
                return ConstantIndex::SYMBOL_STRING_INDEX;
            case TypeOfKind::FUNCTION:
                return ConstantIndex::FUNCTION_STRING_INDEX;
            case TypeOfKind::BIGINT:
                return ConstantIndex::BIGINT_STRING_INDEX;
            case TypeOfKind::NATIVE_MODULE_FAILURE_INFO:
                return ConstantIndex::NATIVE_MODULE_FAILURE_INFO_STRING_INDEX;
            case TypeOfKind::UNKNOWN:
                break;
        }
        UNREACHABLE();
    }

    void LowerTypeOf()
    {
        ValueVertex *obj = frameState.GetAcc();
        TypeOfKind kind = ClassifyTypeOf(obj);
        if (kind == TypeOfKind::UNKNOWN) {
            frameState.SetAcc(CommonStubCall({glue, obj}, CommonStubID::TypeOf, SideEffectKind::SAFE_CALL));
            return;
        }
        frameState.SetAcc(StringFromGlobalConstant(StringIndexOfTypeOfKind(kind)));
    }

    void LowerGetUnmappedArgs()
    {
        ValueVertex *numArgs = ActualArgc();
        ValueVertex *globalEnv = GlobalEnv();

        uint32_t argumentsObjectSize = self->pgoContext_.GetBroker()->GetArgumentsObjectSize();

        constexpr uint64_t slotSize = JSTaggedValue::TaggedTypeSize();
        constexpr uint64_t mandatoryArgc = NUM_MANDATORY_JSFUNC_ARGS;
        static_assert(slotSize == (1U << TAGGED_TYPE_SIZE_LOG));

        const size_t globalConstantsOffset = JSThread::GlueData::GetGlobalConstOffset(false);
        constexpr size_t elementsClassOffset =
            static_cast<size_t>(ConstantIndex::TAGGED_ARRAY_CLASS_INDEX) * slotSize;
        constexpr size_t argumentsClassOffset =
            GlobalEnv::HEADER_SIZE + static_cast<size_t>(GlobalEnv::ARGUMENTS_CLASS) * slotSize;
        ASSERT(globalConstantsOffset <= static_cast<size_t>(std::numeric_limits<int32_t>::max()));
        static_assert(elementsClassOffset <= static_cast<size_t>(std::numeric_limits<int32_t>::max()));
        static_assert(argumentsClassOffset <= static_cast<size_t>(std::numeric_limits<int32_t>::max()));

        ValueVertex *userArgCount = self->NewVertex<I64SubVertex>(
            compileInfoFacts_, currentBlock,
            {numArgs, self->graph_->GetInt64Constant(static_cast<int64_t>(mandatoryArgc))});

        BB *loopHeader = self->NewBlock();
        BB *loopBody = self->NewBlock();
        BB *loopExit = self->NewBlock();
        loopHeader->SetIsLoopHeader(true);

        // The frame ABI guarantees that argc includes the mandatory arguments
        // and that the user argument count fits the INT32 frame representation.
        ValueVertex *globalConstants = self->NewVertex<LoadTaggedFromAddressVertex>(
            compileInfoFacts_, currentBlock, {glue}, static_cast<int32_t>(globalConstantsOffset));
        ValueVertex *elementsHClass = self->NewVertex<LoadTaggedFieldVertex>(
            compileInfoFacts_, currentBlock, {globalConstants}, static_cast<int32_t>(elementsClassOffset));
        ValueVertex *elementsDataSize = self->NewVertex<I64ShiftLeftVertex>(
            compileInfoFacts_, currentBlock, {userArgCount}, TAGGED_TYPE_SIZE_LOG);
        ValueVertex *elementsSize = self->NewVertex<I64AddVertex>(
            compileInfoFacts_, currentBlock,
            {elementsDataSize, self->graph_->GetInt64Constant(static_cast<int64_t>(TaggedArray::DATA_OFFSET))});
        ValueVertex* rawElements = AllocateInYoung(elementsSize, elementsHClass);
        ValueVertex *elements = self->NewVertex<InitializeUnmappedArgumentsElementsVertex>(
            compileInfoFacts_, currentBlock, {numArgs, rawElements, elementsHClass});

        // Element initialization and the following copy loop contain no
        // safepoint. Every tagged slot is written before another allocation
        // can trigger GC.
        ValueVertex *argumentCount = self->NewVertex<LoadInt32FieldVertex>(
            compileInfoFacts_, currentBlock, {elements}, static_cast<int32_t>(TaggedArray::LENGTH_OFFSET));
        JumpVertex *entryJump = self->FinishBlockWithJump(currentBlock, loopHeader);
        // Preserve the runtime allocation's invalidation. Loop writes also
        // invalidate facts, including on the exit reached after any iteration.
        compileInfoFacts_->OnSideEffect();
        CompileInfoFacts* entryFacts = compileInfoFacts_;

        currentBlock = loopHeader;
        compileInfoFacts_ = entryFacts->Clone();
        PhiVertex *indexPhi = self->NewPhiVertex(currentBlock, 2, self->AccIndex());  // 2: entry and backedge inputs
        indexPhi->SetPredecessor(entryJump->GetPredecessorId(), TaggedConstantFromInt32(0));
        ValueVertex *index = self->NewVertex<TaggedIntToI32Vertex>(
            compileInfoFacts_, currentBlock, {indexPhi});
        self->FinishBlockWithBranch<BranchIfInt32CompareVertex>(
            currentBlock, {index, argumentCount}, loopBody, loopExit, Condition::BELOW);

        currentBlock = loopBody;
        compileInfoFacts_ = entryFacts->Clone();
        ValueVertex *argument = self->NewVertex<LoadActualArgumentVertex>(
            compileInfoFacts_, currentBlock, {index});
        self->NewVertex<StoreTaggedElementWithBarrierVertex>(
            compileInfoFacts_, currentBlock, {glue, elements, index, argument},
            ArkSteedWriteBarrierValueKind::Unknown);
        compileInfoFacts_->OnSideEffect();
        ValueVertex *nextIndex = self->NewVertex<I32AddVertex>(
            compileInfoFacts_, currentBlock, {index, self->graph_->GetInt32Constant(1)});
        ValueVertex *nextTaggedIndex = self->NewVertex<I32ToTaggedIntVertex>(
            compileInfoFacts_, currentBlock, {nextIndex});
        JumpLoopVertex *backedge = self->FinishBlockWithJumpLoop(currentBlock, loopHeader);
        indexPhi->SetPredecessor(backedge->GetPredecessorId(), nextTaggedIndex);

        ASSERT(loopHeader->IsLoopHeader());
        ASSERT(loopHeader->PredecessorCount() == 2);  // 2: loop entry and latch
        ASSERT(indexPhi->GetInputCount() == loopHeader->PredecessorCount());
        ASSERT(indexPhi->GetPredecessor(entryJump->GetPredecessorId()) != nullptr);
        ASSERT(indexPhi->GetPredecessor(backedge->GetPredecessorId()) != nullptr);
        ASSERT(backedge->Target() == loopHeader);

        currentBlock = loopExit;
        compileInfoFacts_ = entryFacts->Clone();
        ValueVertex *argumentsHClass = self->NewVertex<LoadTaggedFieldVertex>(
            compileInfoFacts_, currentBlock, {globalEnv}, static_cast<int32_t>(argumentsClassOffset));
        ValueVertex *argumentsSize =
            self->graph_->GetInt64Constant(static_cast<int64_t>(argumentsObjectSize));
        ValueVertex* rawArguments = AllocateInYoung(argumentsSize, argumentsHClass);
        ValueVertex *arguments = self->NewVertex<InitializeUnmappedArgumentsVertex>(
            compileInfoFacts_, currentBlock,
            {glue, numArgs, rawArguments, elements, globalEnv, argumentsHClass});
        compileInfoFacts_->OnSideEffect();
        compileInfoFacts_->EnsureType(arguments, NodeInfo::NodeType::OTHER_JS_RECEIVER);
        frameState.SetAcc(arguments);
    }

    void LowerCopyRestArgs(const BytecodeInfo *bcInfo)
    {
        ValueVertex *taggedArgc = TaggedActualArgc();
        ValueVertex *taggedRestIdx = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 0));
        RuntimeCallToAccWithLazyDeopt({taggedArgc, taggedRestIdx}, RTSTUB_ID(OptCopyRestArgs));
    }

    void LowerDelObjProp(const BytecodeInfo *bcInfo)
    {
        ValueVertex *object = LoadRegister(bcInfo, 0);
        ValueVertex *prop = frameState.GetAcc();
        CommonStubCallToAccWithLazyDeopt({glue, object, prop, GlobalEnv()}, CommonStubID::DeleteObjectProperty);
    }

    void LowerIsIn(const BytecodeInfo *bcInfo)
    {
        ValueVertex *prop = LoadRegister(bcInfo, 0);
        ValueVertex *obj = frameState.GetAcc();
        CommonStubCallToAccWithLazyDeopt({glue, prop, obj, GlobalEnv()}, CommonStubID::IsIn);
    }

    void LowerInstanceOf(const BytecodeInfo *bcInfo)
    {
        ValueVertex *object = LoadRegister(bcInfo, 1);
        ValueVertex *target = frameState.GetAcc();
        CommonStubCallToAccWithICAndLazyDeopt(bcInfo, {object, target, GlobalEnv()}, CommonStubID::Instanceof);
    }

    void LowerTestIn(const BytecodeInfo *bcInfo)
    {
        // 1: level operand index
        ValueVertex *levelIndex = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 1));
        // 2: slot operand index
        ValueVertex *slotIndex = TaggedConstantFromInt32(GetImmediate<int>(bcInfo, 2));
        ValueVertex *lexicalEnv = LoadRegister(bcInfo, 3);  // 3: lexicalEnv register index
        ValueVertex *obj = frameState.GetAcc();
        RuntimeCallToAccWithLazyDeopt({lexicalEnv, levelIndex, slotIndex, obj}, RTSTUB_ID(TestIn));
    }

    void LowerCopyDataProperties(const BytecodeInfo *bcInfo)
    {
        ValueVertex *target = LoadRegister(bcInfo, 0);
        ValueVertex *source = frameState.GetAcc();
        RuntimeCallToAccWithLazyDeopt({target, source}, RTSTUB_ID(CopyDataProperties));
    }

    void LowerStoreArraySpread(const BytecodeInfo *bcInfo)
    {
        ValueVertex *array = LoadRegister(bcInfo, 0);
        ValueVertex *index = LoadRegister(bcInfo, 1);
        ValueVertex *value = frameState.GetAcc();
        RuntimeCallToAccWithLazyDeopt({array, index, value}, RTSTUB_ID(StArraySpread));
    }

    void LowerGetTemplateObject()
    {
        ValueVertex *value = frameState.GetAcc();
        RuntimeCallToAccWithLazyDeopt({value}, RTSTUB_ID(GetTemplateObject));
    }

    void LowerSetObjectWithProto(const BytecodeInfo *bcInfo)
    {
        ValueVertex *proto = LoadRegister(bcInfo, 0);
        ValueVertex *obj = frameState.GetAcc();
        RuntimeCallWithLazyDeopt({proto, obj}, RTSTUB_ID(SetObjectWithProto));
    }

    void LowerNotifyConcurrentResult()
    {
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *result = frameState.GetAcc();
        RuntimeCallWithLazyDeopt({result, jsFunc}, RTSTUB_ID(NotifyConcurrentResult));
    }

    // -------- Category #15: Exceptions --------

    bool TryBuildColdCatchDeopt()
    {
        if (!self->IsLazyDeoptEnabled() || !HasNeverExecutedCatchBlock()) {
            return false;
        }
        constexpr auto DEOPT_TYPE = kungfu::DeoptType::INSUFFICIENTPROFILE;
        auto *deopt = self->FinishBlockWith<DeoptVertex>(currentBlock, {}, self->chunk_, DEOPT_TYPE, currentBcIndex);
        deopt->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(currentBcIndex));
        return true;
    }

    void LowerThrow()
    {
        currentBlock->SetDeferred(true);
        if (!TryBuildColdCatchDeopt()) {
            ValueVertex *exception = frameState.GetAcc();
            auto *vertex = self->FinishBlockWith<ThrowVertex>(currentBlock, {exception}, RTSTUB_ID(Throw));
            UpdateCatchBlockData(vertex);
        }
    }

    void LowerThrowConstAssignment(const BytecodeInfo *bcInfo)
    {
        currentBlock->SetDeferred(true);
        if (!TryBuildColdCatchDeopt()) {
            ValueVertex *value = LoadRegister(bcInfo, 0);
            auto *vertex = self->FinishBlockWith<ThrowVertex>(currentBlock, {value}, RTSTUB_ID(ThrowConstAssignment));
            UpdateCatchBlockData(vertex);
        }
    }

    void LowerThrowNotExists()
    {
        currentBlock->SetDeferred(true);
        if (!TryBuildColdCatchDeopt()) {
            auto *vertex = self->FinishBlockWith<ThrowVertex>(currentBlock, {}, RTSTUB_ID(ThrowThrowNotExists));
            UpdateCatchBlockData(vertex);
        }
    }

    void LowerThrowPatternNonCoercible()
    {
        currentBlock->SetDeferred(true);
        if (!TryBuildColdCatchDeopt()) {
            auto *vertex = self->FinishBlockWith<ThrowVertex>(currentBlock, {}, RTSTUB_ID(ThrowPatternNonCoercible));
            UpdateCatchBlockData(vertex);
        }
    }

    void LowerThrowDeleteSuperProperty()
    {
        currentBlock->SetDeferred(true);
        if (!TryBuildColdCatchDeopt()) {
            auto *vertex = self->FinishBlockWith<ThrowVertex>(currentBlock, {}, RTSTUB_ID(ThrowDeleteSuperProperty));
            UpdateCatchBlockData(vertex);
        }
    }

    enum class LoweringResult : uint8_t { CONTINUE, BLOCK_TERMINATED };

    LoweringResult LowerThrowIfNotObject(const BytecodeInfo *bcInfo)
    {
        ValueVertex *value = LoadRegister(bcInfo, 0);

        constexpr auto JS_RECEIVER = NodeInfo::NodeType::JS_RECEIVER;
        NodeInfo::NodeType knownType = compileInfoFacts_->GetKnownType(value);
        if (!NodeInfo::IsEmptyNodeType(knownType)) {
            if (NodeInfo::NodeTypeIs(knownType, JS_RECEIVER)) {
                compileInfoFacts_->RecordNonHole(value);
                return LoweringResult::CONTINUE;
            }
            if (!NodeInfo::NodeTypeCanBe(knownType, JS_RECEIVER)) {
                currentBlock->SetDeferred(true);
                if (!TryBuildColdCatchDeopt()) {
                    auto *vertex = self->FinishBlockWith<ThrowVertex>(currentBlock, {}, RTSTUB_ID(ThrowIfNotObject));
                    UpdateCatchBlockData(vertex);
                }
                return LoweringResult::BLOCK_TERMINATED;
            }
        }

        BB *isHeapObjectBlock = self->NewBlock();
        BB *checkLowerDoneBlock = self->NewBlock();
        BB *checkUpperDoneBlock = self->NewBlock();

        // Note: each failure branch requires an independent throwing block to prevent critical edges in the subgraph.
        BB *notHeapObjectBlock = self->NewBlock();
        BB *checkLowerFailedBlock = self->NewBlock();
        BB *checkUpperFailedBlock = self->NewBlock();

        self->FinishBlockWithBranch<BranchIfTaggedHeapObjectVertex>(currentBlock, {value}, isHeapObjectBlock,
                                                                    notHeapObjectBlock);

        // Hot path: value is a heap object → check HClass type range inline.
        currentBlock = isHeapObjectBlock;

        ValueVertex *hclass = self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {value},
                                                                     static_cast<int32_t>(TaggedObject::HCLASS_OFFSET));
        ValueVertex *hclassRaw = self->NewVertex<TaggedToRawI64Vertex>(compileInfoFacts_, currentBlock, {hclass});

        ValueVertex *addrMask = self->graph_->GetInt64Constant(static_cast<int64_t>(TaggedObject::GC_STATE_MASK));
        ValueVertex *hclassMasked = self->NewVertex<I64BitwiseBinaryVertex>(
            compileInfoFacts_, currentBlock, {hclassRaw, addrMask}, IntBitwiseKind::BITWISE_AND);

        ValueVertex *bitField = self->NewVertex<LoadTaggedFromAddressVertex>(
            compileInfoFacts_, currentBlock, {hclassMasked}, static_cast<int32_t>(JSHClass::BIT_FIELD_OFFSET));
        ValueVertex *bitFieldRaw = self->NewVertex<TaggedToRawI64Vertex>(compileInfoFacts_, currentBlock, {bitField});

        // Type is encoded to the first 8 bits of JSHClass::bitfield
        static_assert(JSHClass::ObjectTypeBits::START_BIT == 0);
        ValueVertex *typeMask = self->graph_->GetInt64Constant((1U << JSHClass::ObjectTypeBits::SIZE) - 1);
        ValueVertex *typeBits = self->NewVertex<I64BitwiseBinaryVertex>(
            compileInfoFacts_, currentBlock, {bitFieldRaw, typeMask}, IntBitwiseKind::BITWISE_AND);

        // Whether type is in [ECMA_OBJECT_FIRST, ECMA_OBJECT_LAST]
        ValueVertex *firstType = self->graph_->GetInt64Constant(static_cast<int64_t>(JSType::ECMA_OBJECT_FIRST));
        self->FinishBlockWithBranch<BranchIfInt64CompareVertex>(currentBlock, {typeBits, firstType},
                                                                checkLowerDoneBlock, checkLowerFailedBlock,
                                                                Condition::GREATER_THAN_OR_EQUAL);

        currentBlock = checkLowerDoneBlock;
        ValueVertex *lastType = self->graph_->GetInt64Constant(static_cast<int64_t>(JSType::ECMA_OBJECT_LAST));
        self->FinishBlockWithBranch<BranchIfInt64CompareVertex>(currentBlock, {typeBits, lastType}, checkUpperDoneBlock,
                                                                checkUpperFailedBlock, Condition::LESS_THAN_OR_EQUAL);

        for (BB *exceptionBlock : {notHeapObjectBlock, checkLowerFailedBlock, checkUpperFailedBlock}) {
            currentBlock = exceptionBlock;
            currentBlock->SetDeferred(true);
            if (!TryBuildColdCatchDeopt()) {
                auto *vertex = self->FinishBlockWith<ThrowVertex>(currentBlock, {}, RTSTUB_ID(ThrowIfNotObject));
                UpdateCatchBlockData(vertex);
            }
        }

        // Success: value is an ECMA object.
        currentBlock = checkUpperDoneBlock;
        compileInfoFacts_->EnsureType(value, JS_RECEIVER);
        return LoweringResult::CONTINUE;
    }

    enum class HoleCheckKind : uint8_t { ELIDED, ALWAYS_THROWS, NEEDED };

    HoleCheckKind ClassifyHoleCheck(ValueVertex *receiver)
    {
        if (compileInfoFacts_->IsKnownNonHole(receiver)) {
            return HoleCheckKind::ELIDED;
        }
        if (auto *constant = receiver->TryCast<TaggedConstantVertex>(); constant != nullptr) {
            return constant->GetValue() == JSTaggedValue::VALUE_HOLE ? HoleCheckKind::ALWAYS_THROWS
                                                                     : HoleCheckKind::ELIDED;
        }
        NodeInfo::NodeType knownType = compileInfoFacts_->GetKnownType(receiver);
        if (!NodeInfo::IsEmptyNodeType(knownType) && knownType != NodeInfo::NodeType::UNKNOWN) {
            return HoleCheckKind::ELIDED;
        }
        return HoleCheckKind::NEEDED;
    }

    LoweringResult LowerThrowUndefinedIfHole(const BytecodeInfo *bcInfo)
    {
        ValueVertex *receiver = LoadRegister(bcInfo, 0);
        ValueVertex *obj = LoadRegister(bcInfo, 1);

        switch (ClassifyHoleCheck(receiver)) {
            case HoleCheckKind::ELIDED:
                compileInfoFacts_->RecordNonHole(receiver);
                return LoweringResult::CONTINUE;
            case HoleCheckKind::ALWAYS_THROWS:
                currentBlock->SetDeferred(true);
                if (!TryBuildColdCatchDeopt()) {
                    auto *throwVertex =
                        self->FinishBlockWith<ThrowVertex>(currentBlock, {obj}, RTSTUB_ID(ThrowUndefinedIfHole));
                    UpdateCatchBlockData(throwVertex);
                }
                return LoweringResult::BLOCK_TERMINATED;
            case HoleCheckKind::NEEDED:
                break;
        }

        BB *throwBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();

        ValueVertex *hole = self->graph_->GetTaggedConstant(JSTaggedValue::VALUE_HOLE);
        self->FinishBlockWith<BranchIfReferenceEqualVertex>(currentBlock, {receiver, hole}, throwBlock, doneBlock);

        currentBlock = throwBlock;
        currentBlock->SetDeferred(true);
        if (!TryBuildColdCatchDeopt()) {
            auto *throwVertex =
                self->FinishBlockWith<ThrowVertex>(currentBlock, {obj}, RTSTUB_ID(ThrowUndefinedIfHole));
            UpdateCatchBlockData(throwVertex);
        }
        currentBlock = doneBlock;
        compileInfoFacts_->RecordNonHole(receiver);
        return LoweringResult::CONTINUE;
    }

    LoweringResult LowerThrowUndefinedIfHoleWithName(const BytecodeInfo *bcInfo)
    {
        ValueVertex *receiver = frameState.GetAcc();

        switch (ClassifyHoleCheck(receiver)) {
            case HoleCheckKind::ELIDED:
                compileInfoFacts_->RecordNonHole(receiver);
                return LoweringResult::CONTINUE;
            case HoleCheckKind::ALWAYS_THROWS:
                currentBlock->SetDeferred(true);
                if (!TryBuildColdCatchDeopt()) {
                    ValueVertex *strID = self->graph_->GetInt32Constant(GetICSlotId<int>(bcInfo, 0));
                    ValueVertex *str = StringFromConstPool(strID);
                    auto *throwVertex =
                        self->FinishBlockWith<ThrowVertex>(currentBlock, {str}, RTSTUB_ID(ThrowUndefinedIfHole));
                    UpdateCatchBlockData(throwVertex);
                }
                return LoweringResult::BLOCK_TERMINATED;
            case HoleCheckKind::NEEDED:
                break;
        }

        BB *throwBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();

        ValueVertex *hole = self->graph_->GetTaggedConstant(JSTaggedValue::VALUE_HOLE);
        self->FinishBlockWithBranch<BranchIfReferenceEqualVertex>(currentBlock, {receiver, hole}, throwBlock,
                                                                  doneBlock);

        currentBlock = throwBlock;
        currentBlock->SetDeferred(true);
        if (!TryBuildColdCatchDeopt()) {
            ValueVertex *strID = self->graph_->GetInt32Constant(GetICSlotId<int>(bcInfo, 0));
            ValueVertex *str = StringFromConstPool(strID);
            auto *throwVertex =
                self->FinishBlockWith<ThrowVertex>(currentBlock, {str}, RTSTUB_ID(ThrowUndefinedIfHole));
            UpdateCatchBlockData(throwVertex);
        }

        currentBlock = doneBlock;
        compileInfoFacts_->RecordNonHole(receiver);
        return LoweringResult::CONTINUE;
    }

    void LowerThrowIfSuperNotCorrectCall(const BytecodeInfo *bcInfo)
    {
        int index = GetImmediate<int>(bcInfo, 0);
        ValueVertex *thisValue = frameState.GetAcc();

        if (index != 0 && index != 1) {
            ValueVertex *indexValue = TaggedConstantFromInt32(index);
            RuntimeCallWithLazyDeopt({indexValue, thisValue}, RTSTUB_ID(ThrowIfSuperNotCorrectCall));
            return;
        }

        bool throwOnMatch = (index == 0);
        ValueVertex *undefined = self->undefinedValue_;
        ValueVertex *hole = self->graph_->GetTaggedConstant(JSTaggedValue::VALUE_HOLE);
        ValueVertex *indexValue = TaggedConstantFromInt32(index);

        BB *undefinedMatchBlock = self->NewBlock();
        BB *notUndefinedBlock = self->NewBlock();
        BB *holeMatchBlock = self->NewBlock();
        BB *okBlock = self->NewBlock();

        self->FinishBlockWithBranch<BranchIfReferenceEqualVertex>(currentBlock, {thisValue, undefined},
                                                                  undefinedMatchBlock, notUndefinedBlock);

        auto emitThrow = [&]() {
            currentBlock->SetDeferred(true);
            if (!TryBuildColdCatchDeopt()) {
                auto *vertex = self->FinishBlockWith<ThrowVertex>(currentBlock, {indexValue, thisValue},
                                                                  RTSTUB_ID(ThrowIfSuperNotCorrectCall));
                UpdateCatchBlockData(vertex);
            }
        };

        currentBlock = notUndefinedBlock;
        if (throwOnMatch) {
            self->FinishBlockWithBranch<BranchIfReferenceEqualVertex>(currentBlock, {thisValue, hole}, holeMatchBlock,
                                                                      okBlock);
        } else {
            BB *throwBlock = self->NewBlock();
            self->FinishBlockWithBranch<BranchIfReferenceEqualVertex>(currentBlock, {thisValue, hole}, holeMatchBlock,
                                                                      throwBlock);
            currentBlock = throwBlock;
            emitThrow();
        }

        currentBlock = undefinedMatchBlock;
        if (throwOnMatch) {
            emitThrow();
        } else {
            self->FinishBlockWithJump(currentBlock, okBlock);
        }

        currentBlock = holeMatchBlock;
        if (throwOnMatch) {
            emitThrow();
        } else {
            self->FinishBlockWithJump(currentBlock, okBlock);
        }

        currentBlock = okBlock;
    }

    // -------- Category #16: Control Flow --------

    void LowerJumpIfZero()
    {
        ValueVertex *acc = frameState.GetAcc();
        if (auto *asConstant = acc->TryCast<TaggedConstantVertex>(); asConstant != nullptr) {
            uint64_t rawValue = asConstant->GetValue();

            if (rawValue == JSTaggedValue::VALUE_TRUE) {
                LOG_COMPILER(DEBUG) << "LowerJumpIfZero(): TRUE -> Fallthrough";
                self->FinishBlockWithJump(currentBlock, FallthroughTarget());
                return;
            }
            if (rawValue == JSTaggedValue::VALUE_FALSE) {
                LOG_COMPILER(DEBUG) << "LowerJumpIfZero(): FALSE -> Jump";
                self->FinishBlockWithJump(currentBlock, JumpTarget());
                return;
            }
        }
        self->FinishBlockWithBranch(currentBlock, acc, FallthroughTarget(), JumpTarget());
    }

    void LowerJumpIfNonZero()
    {
        ValueVertex *acc = frameState.GetAcc();
        if (auto *asConstant = acc->TryCast<TaggedConstantVertex>(); asConstant != nullptr) {
            uint64_t rawValue = asConstant->GetValue();

            if (rawValue == JSTaggedValue::VALUE_TRUE) {
                LOG_COMPILER(DEBUG) << "LowerJumpIfNonZero(): TRUE -> Jump";
                self->FinishBlockWithJump(currentBlock, JumpTarget());
                return;
            }
            if (rawValue == JSTaggedValue::VALUE_FALSE) {
                LOG_COMPILER(DEBUG) << "LowerJumpIfNonZero(): FALSE -> Fallthrough";
                self->FinishBlockWithJump(currentBlock, FallthroughTarget());
                return;
            }
        }
        self->FinishBlockWithBranch(currentBlock, acc, JumpTarget(), FallthroughTarget());
    }

    void LowerJumpConstant()
    {
        ASSERT(blockInfo->IsJump());
        if (blockInfo->jumpBlock->loopBackBlock == blockInfo) {
            self->FinishLoopBackedge(currentBlock, frameState, compileInfoFacts_, blockInfo->jumpBlock->rpoIndex);
            return;
        }
        self->FinishBlockWithJump(currentBlock, self->ActivateNonCatchBlock(blockInfo->jumpBlock->rpoIndex));
    }

    // -------- Deoptimization Helpers --------

    using LazyDeoptFrameState = LazyDeoptimizableMixin::LazyDeoptFrameState;
    using EagerDeoptFrameState = EagerDeoptimizableMixin::EagerDeoptFrameState;

    DeoptTranslationKind GetDeoptValueKind(ValueVertex *value) const
    {
        switch (value->GetValueRepresentation()) {
            case ValueRepresentation::TAGGED:
                return DeoptTranslationKind::TAGGED;
            case ValueRepresentation::INT32:
                return DeoptTranslationKind::INT32_TO_TAGGED;
            case ValueRepresentation::FLOAT64:
            case ValueRepresentation::HOLEY_FLOAT64:
                return DeoptTranslationKind::FLOAT64_TO_TAGGED_DOUBLE;
            case ValueRepresentation::UINT32:
            case ValueRepresentation::INT64:
            case ValueRepresentation::NONE:
                break;
        }
        UNREACHABLE();
    }

    template <class DeoptFrameState>
    void AppendDeoptInput(DeoptFrameState *deoptFrameState, int32_t id, ValueVertex *value)
    {
        ValueVertex *frameValue = value == nullptr ? self->undefinedValue_ : value;
        deoptFrameState->emplace_back(id, frameValue, GetDeoptValueKind(frameValue));
    }

    template <class DeoptFrameState>
    void AppendCommonDeoptInputs(uint32_t bcIndex, DeoptFrameState *deoptFrameState)
    {
        AppendDeoptInput(deoptFrameState, static_cast<int32_t>(SpecVregIndex::FUNC_INDEX),
                         LoadParam(CALL_TARGET_PARAM_INDEX));
        AppendDeoptInput(deoptFrameState, static_cast<int32_t>(SpecVregIndex::NEWTARGET_INDEX),
                         LoadParam(NEW_TARGET_PARAM_INDEX));
        AppendDeoptInput(deoptFrameState, static_cast<int32_t>(SpecVregIndex::THIS_OBJECT_INDEX),
                         LoadParam(THIS_OBJECT_PARAM_INDEX));
        int32_t bcOffset = static_cast<int32_t>(self->preproc_->GetBytecodeOffset(bcIndex));
        deoptFrameState->emplace_back(static_cast<int32_t>(SpecVregIndex::PC_OFFSET_INDEX),
                                      self->graph_->GetInt32Constant(bcOffset), DeoptTranslationKind::RAW_INT32);
    }

    void AppendLazyCommonDeoptInputs(uint32_t bcIndex, LazyDeoptFrameState *deoptFrameState)
    {
        AppendCommonDeoptInputs(bcIndex, deoptFrameState);
        AppendDeoptInput(deoptFrameState, static_cast<int32_t>(SpecVregIndex::ACTUAL_ARGC_INDEX), TaggedActualArgc());
    }

    void AppendEagerCommonDeoptInputs(uint32_t bcIndex, EagerDeoptFrameState *deoptFrameState)
    {
        AppendCommonDeoptInputs(bcIndex, deoptFrameState);
        ValueVertex *lexicalEnv = frameState.GetLexicalEnv();
        AppendDeoptInput(deoptFrameState, static_cast<int32_t>(SpecVregIndex::ENV_INDEX),
                         lexicalEnv == self->initialLexicalEnv_ ? self->undefinedValue_ : lexicalEnv);
        deoptFrameState->emplace_back(static_cast<int32_t>(SpecVregIndex::ACTUAL_ARGC_INDEX), self->initialActualArgc_,
                                      DeoptTranslationKind::INT32_TO_TAGGED);
    }

    template <class DeoptFrameState, class Predicate>
    void AppendLiveLocalsAndParams(DeoptFrameState *deoptFrameState, VRegIDType firstParamIndex, Predicate predicate)
    {
        for (VRegIDType i = 0; i < self->numLocal_; i++) {
            VRegIDType localIndex = VRegOfLocal(i);
            if (predicate(localIndex)) {
                AppendDeoptInput(deoptFrameState, static_cast<int32_t>(localIndex), frameState.Get(localIndex));
            }
        }
        for (VRegIDType i = firstParamIndex; i < self->numParams_; i++) {
            VRegIDType paramIndex = VRegOfParam(self->numLocal_, i);
            if (predicate(paramIndex)) {
                AppendDeoptInput(deoptFrameState, static_cast<int32_t>(paramIndex), LoadParam(i));
            }
        }
    }

    // D refers to dependency
    void BuildLazyDeoptInputsForDOnly(uint32_t bcIndex, LazyDeoptFrameState *deoptFrameState)
    {
        const BytecodeInfo *bcInfo = self->preproc_->GetBytecode(bcIndex);
        const auto &liveOut = self->analysis_->GetLiveOutOfBytecode(bcIndex);

        auto isInVRegOut = [bcInfo](VRegIDType index) {
            return (bcInfo->vregOut != BytecodeInfo::NO_VREG) && (bcInfo->vregOut == index);
        };

        AppendLazyCommonDeoptInputs(bcIndex, deoptFrameState);
        VRegIDType envIndex = self->LexicalEnvIndex();
        if (!bcInfo->EnvOut() && !isInVRegOut(envIndex)) {
            constexpr int32_t ENV_INDEX = static_cast<int32_t>(SpecVregIndex::ENV_INDEX);
            AppendDeoptInput(deoptFrameState, ENV_INDEX, frameState.GetLexicalEnv());
        }
        if (!bcInfo->AccOut()) {
            constexpr int32_t ACC_INDEX = static_cast<int32_t>(SpecVregIndex::ACC_INDEX);
            AppendDeoptInput(deoptFrameState, ACC_INDEX, frameState.GetAcc());
        }
        AppendLiveLocalsAndParams(deoptFrameState, 0, [&liveOut, &isInVRegOut](VRegIDType index) {
            return liveOut.TestBit(index) && !isInVRegOut(index);
        });
    }

    // E refers to exception
    void BuildLazyDeoptInputsForEOnly(uint32_t bcIndex, LazyDeoptFrameState *deoptFrameState)
    {
        ASSERT(blockInfo->catchBlock != nullptr);
        // Exception lazy-deopt resumes at the catch handler entry. ACC is restored as the exception object by
        // the lazy-deopt trampoline, not from this payload.
        const kungfu::BitSet &catchLiveIn = self->analysis_->GetLiveInOfBlock(blockInfo->catchBlock->rpoIndex);
        AppendLazyCommonDeoptInputs(bcIndex, deoptFrameState);
        AppendDeoptInput(deoptFrameState, static_cast<int32_t>(SpecVregIndex::ENV_INDEX), frameState.GetLexicalEnv());
        AppendLiveLocalsAndParams(deoptFrameState, 0,
                                  [&catchLiveIn](VRegIDType index) { return catchLiveIn.TestBit(index); });
    }

    // D+E (dependency + exception) lazy deopt.
    // Lazy-deopt takes live-out - {ACC} which is equivalent to live-in - {ACC} with Ark bytecode.
    void BuildLazyDeoptInputsForDE(uint32_t bcIndex, LazyDeoptFrameState *deoptFrameState, bool includeAcc)
    {
        kungfu::BitSet liveSet(self->chunk_, self->analysis_->GetNumVRegs());
        liveSet.CopyFrom(self->analysis_->GetLiveInOfBytecode(bcIndex));
        if (blockInfo->catchBlock != nullptr) {
            liveSet.Union(self->analysis_->GetLiveInOfBlock(blockInfo->catchBlock->rpoIndex));
        }
        AppendLazyCommonDeoptInputs(bcIndex, deoptFrameState);
        AppendDeoptInput(deoptFrameState, static_cast<int32_t>(SpecVregIndex::ENV_INDEX), frameState.GetLexicalEnv());
        if (includeAcc) {
            AppendDeoptInput(deoptFrameState, static_cast<int32_t>(SpecVregIndex::ACC_INDEX), frameState.GetAcc());
        }
        AppendLiveLocalsAndParams(deoptFrameState, 0, [&liveSet](VRegIDType index) { return liveSet.TestBit(index); });
    }

    EagerDeoptFrameState BuildCurrentEagerDeoptFrameState(uint32_t bcIndex)
    {
        EagerDeoptFrameState frameStateValues {self->chunk_};
        kungfu::BitSet liveSet(self->chunk_, self->analysis_->GetNumVRegs());
        liveSet.CopyFrom(self->analysis_->GetLiveInOfBytecode(bcIndex));
        if (blockInfo->catchBlock != nullptr) {
            liveSet.Union(self->analysis_->GetLiveInOfBlock(blockInfo->catchBlock->rpoIndex));
        }

        AppendEagerCommonDeoptInputs(bcIndex, &frameStateValues);
        if (liveSet.TestBit(self->AccIndex())) {
            AppendDeoptInput(&frameStateValues, static_cast<int32_t>(SpecVregIndex::ACC_INDEX), frameState.GetAcc());
        }
        AppendLiveLocalsAndParams(&frameStateValues, FIXED_PARAM_VREG_COUNT,
                                  [&liveSet](VRegIDType index) { return liveSet.TestBit(index); });
        return frameStateValues;
    }

    template <class CallT>
    bool InputMayBeJSReceiver(CallT *call, size_t index) const
    {
        constexpr auto JS_RECEIVER = NodeInfo::NodeType::JS_RECEIVER;
        const NodeInfo::NodeType knownType = compileInfoFacts_->GetKnownType(call->GetInput(index));
        return NodeInfo::NodeTypeCanBe(knownType, JS_RECEIVER);
    }

    bool IsDependencySafeCommonStubCall(CallCommonStubVertex *call) const
    {
        constexpr auto JS_RECEIVER = NodeInfo::NodeType::JS_RECEIVER;
        switch (call->GetCommonStubID()) {
            // Strict equality is a reference/value comparison and never runs user-defined conversion.
            case CommonStubID::StrictEqual:
            case CommonStubID::StrictNotEqual:
                return true;
            // Boolean conversion follows ToBoolean and does not call user-defined conversion hooks.
            case CommonStubID::ToBooleanTrue:
            case CommonStubID::ToBooleanFalse:
                return true;
            // String-only conversion cannot run user hooks. It returns an intermediate
            // number before wrapper allocation, not the completed newobjrange result.
            case CommonStubID::ArkSteedStringToNumber:
                return compileInfoFacts_->CheckType(call->GetInput(1), NodeInfo::NodeType::STRING);
            // typeof is observable only through the input value category and does not mutate dependencies.
            case CommonStubID::TypeOf:
                return true;
            // A capacity slow call may run a native finalizer that invalidates dependencies.
            case CommonStubID::CreateEmptyArray:
            case CommonStubID::CreateArrayWithBuffer:
            case CommonStubID::ArkSteedNewObjectFromPrimitive:
                return false;
            // These allocation/constant-pool helpers create or load values without observing receiver hooks.
            case CommonStubID::CreateObjectHavingMethod:
            case CommonStubID::Definefunc:
            case CommonStubID::GetObjectFromConstPool:
            case CommonStubID::GetStringFromConstPool:
            case CommonStubID::GetUnmappedArgs:
            case CommonStubID::FastNewThisObject:
            case CommonStubID::NewLexicalEnv:
            case CommonStubID::NewFloat32ArrayWithNoArgs:
                return true;
            // Abstract equality may call user-defined conversion on mixed object/non-object operands.
            // Unlike relational comparison, object-object equality is a reference check and skips ToPrimitive.
            case CommonStubID::Equal:
            case CommonStubID::NotEqual: {
                // 1 : left operand, after glue.
                ValueVertex *left = call->GetInput(1);
                // 2 : right operand, after glue and left operand.
                ValueVertex *right = call->GetInput(2);
                if (!NodeInfo::NodeTypeCanBe(compileInfoFacts_->GetKnownType(left), JS_RECEIVER) &&
                    !NodeInfo::NodeTypeCanBe(compileInfoFacts_->GetKnownType(right), JS_RECEIVER)) {
                    // If neither side can be an object, abstract equality cannot run user-defined conversion.
                    return true;
                }
                // If both sides are definitely objects, equality is a reference check and still skips conversion.
                return compileInfoFacts_->CheckType(left, JS_RECEIVER) &&
                       compileInfoFacts_->CheckType(right, JS_RECEIVER);
            }
            // Unary numeric stubs may run ToNumber/ToNumeric on the accumulator operand.
            case CommonStubID::Inc:
            case CommonStubID::Dec:
            case CommonStubID::Neg:
            case CommonStubID::Not:
                // 1 : accumulator operand, after glue.
                return !InputMayBeJSReceiver(call, 1);
            // Binary arithmetic and bitwise stubs may run ToPrimitive/ToNumeric/ToNumber on either operand.
            case CommonStubID::Add:
            case CommonStubID::Sub:
            case CommonStubID::Mul:
            case CommonStubID::Div:
            case CommonStubID::Mod:
            case CommonStubID::And:
            case CommonStubID::Or:
            case CommonStubID::Xor:
            case CommonStubID::Shl:
            case CommonStubID::Shr:
            case CommonStubID::Ashr:
            case CommonStubID::StringAdd:
                // 1 : left operand, after glue.
                // 2 : right operand, after glue and left operand.
                return !InputMayBeJSReceiver(call, 1) && !InputMayBeJSReceiver(call, 2);
            // Relational comparison runs ToPrimitive even for object-object inputs, so it cannot use the
            // object-object equality fast rejection above.
            case CommonStubID::Less:
            case CommonStubID::LessEq:
            case CommonStubID::Greater:
            case CommonStubID::GreaterEq:
                // 1 : left operand, after glue.
                // 2 : right operand, after glue and left operand.
                return !InputMayBeJSReceiver(call, 1) && !InputMayBeJSReceiver(call, 2);
            // The `in` operator can consult receiver-side property lookup hooks.
            case CommonStubID::IsIn:
                // 1 : property key operand, after glue.
                // 2 : object operand, after glue and property key.
                return !InputMayBeJSReceiver(call, 1) && !InputMayBeJSReceiver(call, 2);
            // `instanceof` may call @@hasInstance on the constructor operand.
            case CommonStubID::Instanceof:
                // 2 : constructor operand, after glue and object operand.
                return !InputMayBeJSReceiver(call, 2);
            // Define/st-own operations may run receiver/prototype/proxy paths or mutate object shapes.
            case CommonStubID::DefineField:
            case CommonStubID::StOwnByValue:
            case CommonStubID::StOwnByIndex:
            case CommonStubID::StOwnByName:
            case CommonStubID::StOwnByValueWithNameSet:
            case CommonStubID::StOwnByNameWithNameSet:
                // 1 : receiver operand, after glue.
                // 2 : property key operand, after glue and receiver.
                return !InputMayBeJSReceiver(call, 1) && !InputMayBeJSReceiver(call, 2);
            // Delete can mutate the receiver shape; the key may also run conversion logic.
            case CommonStubID::DeleteObjectProperty:
                // 1 : receiver operand, after glue.
                // 2 : property key operand, after glue and receiver.
                return !InputMayBeJSReceiver(call, 1) && !InputMayBeJSReceiver(call, 2);
            default:
                return false;
        }
    }

    bool IsDependencySafeRuntimeCall(CallRuntimeVertex *call) const
    {
        switch (call->GetRuntimeStubID()) {
            // Allocation and metadata helpers below do not execute user JS and do not invalidate dependencies.
            case RTSTUB_ID(GetMethodFromCache):
            case RTSTUB_ID(LdExternalModuleVarByIndexOnJSFunc):
            case RTSTUB_ID(LdLocalModuleVarByIndexOnJSFunc):
            case RTSTUB_ID(LdPatchVar):
            case RTSTUB_ID(OptNewLexicalEnvWithName):
            case RTSTUB_ID(PrintMethodName):
                return true;
            // NumberToString is dependency-safe only when the input cannot be a receiver.
            case RTSTUB_ID(NumberToString):
                // 0 : converted value operand.
                return !InputMayBeJSReceiver(call, 0);
            // Conversion stubs may invoke user-defined valueOf/toString/Symbol.toPrimitive on receiver inputs.
            case RTSTUB_ID(ToNumber):
            case RTSTUB_ID(ToNumeric):
            case RTSTUB_ID(ToPropertyKey):
                // 0 : converted value operand.
                return !InputMayBeJSReceiver(call, 0);
            // Iterator slow paths may perform property access or iterator-return callbacks on the receiver.
            case RTSTUB_ID(GetNextPropNameSlowpath):
                // 0 : iterator or receiver operand.
                return !InputMayBeJSReceiver(call, 0);
            // Changing an object's prototype directly invalidates hidden-class/prototype dependencies.
            case RTSTUB_ID(SetObjectWithProto):
                // 1 : object operand, after prototype operand.
                return !InputMayBeJSReceiver(call, 1);
            // Defining accessors mutates the receiver object's own-property layout.
            case RTSTUB_ID(DefineGetterSetterByValue):
                // 0 : receiver object operand.
                // 1 : property key operand.
                return !InputMayBeJSReceiver(call, 0) && !InputMayBeJSReceiver(call, 1);
            // Object spread copies through own-property reads and define-own-property writes.
            case RTSTUB_ID(CopyDataProperties):
                // 0 : target object operand.
                // 1 : source object operand.
                return !InputMayBeJSReceiver(call, 0) && !InputMayBeJSReceiver(call, 1);
            // Array spread writes elements to the destination array.
            case RTSTUB_ID(StArraySpread):
                // 0 : destination array operand.
                return !InputMayBeJSReceiver(call, 0);
            // Exponentiation may run ToNumeric on either operand.
            case RTSTUB_ID(Exp):
                // 0 : left operand.
                // 1 : right operand.
                return !InputMayBeJSReceiver(call, 0) && !InputMayBeJSReceiver(call, 1);
            default:
                return false;
        }
    }

    void LoadLazyDeoptFrameStateForThrowableCall(uint32_t bcIndex, Vertex *vertex)
    {
        if (!self->IsLazyDeoptEnabled()) {
            return;
        }
        LazyDeoptimizableMixin *deoptMixin = LazyDeoptimizableMixinOf(vertex);
        if (deoptMixin == nullptr) {
            return;
        }
        // E refers to exception lazy-deopt
        bool needsE = HasNeverExecutedCatchBlock();
        // D refers to dependency lazy-deopt.
        bool needsD = vertex->Is<CallVertex>() || vertex->Is<CallKnownJSFunctionVertex>() ||
                      vertex->Is<CallKnownBuiltinVertex>() || vertex->Is<CallSelfVertex>() ||
                      vertex->Is<CallBaseConstructorVertex>() || vertex->Is<CallDerivedConstructorVertex>() ||
                      vertex->Is<CallNewObjRangeBaseVertex>() || vertex->Is<CallNewObjRangeDerivedVertex>();
        if (auto *call = vertex->TryCast<CallCommonStubVertex>()) {
            needsD = !IsDependencySafeCommonStubCall(call);
        } else if (auto *call = vertex->TryCast<CallRuntimeVertex>()) {
            needsD = !IsDependencySafeRuntimeCall(call);
        }
        if (!needsE && !needsD) {
            return;
        }
        ThrowableMixin *throwableMixin = ThrowableMixinOf(vertex);
        ASSERT(!needsE || throwableMixin != nullptr);
        if (!deoptMixin->HasLazyDeoptFrameState()) {
            auto *deoptFrameState = self->chunk_->New<LazyDeoptFrameState>(self->chunk_);
            if (needsE && needsD) {
                // D+E uses one live-in payload. We assume only ACC may be modified by the bytecode,
                // so live-in and live-out are equivalent with ACC excluded.
                BuildLazyDeoptInputsForDE(bcIndex, deoptFrameState, false);
            } else if (needsE) {
                // E-only resumes at the catch handler entry, so only the handler live-ins are required.
                BuildLazyDeoptInputsForEOnly(bcIndex, deoptFrameState);
            } else {
                // D-only uses live-out payload
                BuildLazyDeoptInputsForDOnly(bcIndex, deoptFrameState);
            }
            deoptMixin->SetLazyDeoptFrameState(deoptFrameState, self->preproc_->GetBytecodeOffset(bcIndex));
        }
        if (needsE && !throwableMixin->HasExceptionLazyDeopt()) {
            ASSERT(!throwableMixin->HasCatchBlock());
            throwableMixin->MarkExceptionLazyDeopt();
        }
    }

    // -------- Miscellaneous Helpers --------

    bool HasCatchBlock() const
    {
        return reinterpret_cast<uintptr_t>(lazyCatchBlock) != NO_CATCH_BLOCK_TAG;
    }

    bool HasNeverExecutedCatchBlock() const
    {
        return HasCatchBlock() && blockInfo->catchBlockState == CatchBlockProfileState::NEVER_EXECUTED;
    }

    BB *JumpTarget() const
    {
        ASSERT(blockInfo->IsJump());
        return self->ActivateNonCatchBlock(blockInfo->jumpBlock->rpoIndex);
    }

    BB *FallthroughTarget() const
    {
        ASSERT(blockInfo->HasFallthrough());
        return self->ActivateNonCatchBlock(blockInfo->fallthroughBlock->rpoIndex);
    }

    template <class BranchVertexT, class BuildTrue, class BuildFalse, class... BranchArgs>
    ValueVertex *BuildSelect(std::initializer_list<ValueVertex *> branchInputs, VRegIDType resultVreg,
                             BuildTrue buildTrue, BuildFalse buildFalse, BranchArgs &&...branchArgs)
    {
        BB *trueBlock = self->NewBlock();
        BB *falseBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();
        BB *branchBlock = currentBlock;

        self->FinishBlockWith<BranchVertexT>(branchBlock, branchInputs, trueBlock, falseBlock,
                                             std::forward<BranchArgs>(branchArgs)...);
        trueBlock->AddPredecessor(branchBlock);
        falseBlock->AddPredecessor(branchBlock);

        currentBlock = trueBlock;
        ValueVertex *trueResult = buildTrue();
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = falseBlock;
        ValueVertex *falseResult = buildFalse();
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = doneBlock;
        return self->NewPhiVertexWith(currentBlock, {trueResult, falseResult}, resultVreg);
    }

    template <class BranchVertexT, class BuildTrue, class BuildFalse, class... BranchArgs>
    ValueVertex *BuildSelectWithFacts(std::initializer_list<ValueVertex *> branchInputs, VRegIDType resultVreg,
                                      BuildTrue buildTrue, BuildFalse buildFalse, BranchArgs &&...branchArgs)
    {
        BB *trueBlock = self->NewBlock();
        BB *falseBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();
        CompileInfoFacts *entryFacts = compileInfoFacts_;

        self->FinishBlockWithBranch<BranchVertexT>(currentBlock, branchInputs, trueBlock, falseBlock,
                                                   std::forward<BranchArgs>(branchArgs)...);

        currentBlock = trueBlock;
        compileInfoFacts_ = entryFacts->Clone();
        ValueVertex *trueResult = buildTrue();
        CompileInfoFacts *mergedFacts = compileInfoFacts_;
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = falseBlock;
        compileInfoFacts_ = entryFacts->Clone();
        ValueVertex *falseResult = buildFalse();
        mergedFacts->Merge(*compileInfoFacts_);
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = doneBlock;
        compileInfoFacts_ = mergedFacts;
        return self->NewPhiVertexWith(currentBlock, {trueResult, falseResult}, resultVreg);
    }

    ValueVertex *LoadRegister(const BytecodeInfo *bcInfo, int inputIndex) const
    {
        auto *vreg = std::get_if<VirtualRegister>(bcInfo->inputs.data() + inputIndex);
        ASSERT(vreg != nullptr);
        return frameState.Get(vreg->GetId());
    }

    ValueVertex *LoadParam(VRegIDType paramIndex) const
    {
        VRegIDType vreg = VRegOfParam(self->numLocal_, paramIndex);
        return frameState.Get(vreg);
    }

    template <class CastsTo = uint16_t>
    CastsTo GetConstDataId(const BytecodeInfo *bcInfo, int inputIndex) const
    {
        auto *constDataId = std::get_if<kungfu::ConstDataId>(bcInfo->inputs.data() + inputIndex);
        ASSERT(constDataId != nullptr);
        return static_cast<CastsTo>(constDataId->GetId());
    }

    template <class CastsTo = kungfu::ICSlotIdType>
    CastsTo GetICSlotId(const BytecodeInfo *bcInfo, int inputIndex) const
    {
        auto *icSlotId = std::get_if<kungfu::ICSlotId>(bcInfo->inputs.data() + inputIndex);
        ASSERT(icSlotId != nullptr);
        return static_cast<CastsTo>(icSlotId->GetId());
    }

    template <class CastsTo = kungfu::ImmValueType>
    CastsTo GetImmediate(const BytecodeInfo *bcInfo, int inputIndex) const
    {
        auto *imm = std::get_if<kungfu::Immediate>(bcInfo->inputs.data() + inputIndex);
        ASSERT(imm != nullptr);
        return static_cast<CastsTo>(imm->GetValue());
    }

    ValueVertex *GlobalEnv()
    {
        if (UNLIKELY(lazyGlobalEnv == nullptr)) {
            lazyGlobalEnv = self->ActivateGlobalEnv();  // Update self->lazyGlobalEnv_
        }
        return lazyGlobalEnv;
    }

    ValueVertex *ActualArgc()
    {
        ASSERT(self->actualArgc_ != nullptr);
        return self->actualArgc_;
    }

    ValueVertex *TaggedActualArgc()
    {
        ASSERT(self->taggedActualArgc_ != nullptr);
        return self->taggedActualArgc_;
    }

    std::optional<ArkSteedHeapRef> TryGetConstantHeapRef(ValueVertex *node) const
    {
        if (node == nullptr || !node->IsTagged()) {
            return std::nullopt;
        }

        if (auto *constant = node->TryCast<TaggedConstantVertex>()) {
            JSTaggedValue value(constant->GetValue());
            if (value.IsHole()) {
                return ArkSteedHeapRef::FromImmediate(value);
            }
            // Heap constants must be rooted before entering the graph.
            ASSERT(!value.IsHeapObject());
            return std::nullopt;
        }
        if (auto *constant = node->TryCast<HeapConstantVertex>()) {
            JitCompilationEnv *env = self->preproc_->GetEnv();
            if (env == nullptr || constant->GetHandleIndex() >= env->GetHeapConstantTable().size()) {
                return std::nullopt;
            }
            auto *broker = self->pgoContext_.GetBroker();
            if (broker == nullptr) {
                return std::nullopt;
            }
            ArkSteedHeapRef ref = broker->MakeObjectRef(env->GetHeapConstantHandle(constant->GetHandleIndex()));
            return ref.IsSafeForCompile() ? std::optional<ArkSteedHeapRef>(ref) : std::nullopt;
        }
        return std::nullopt;
    }

    bool IsEmptyStringRef(const ArkSteedHeapRef &stringRef) const
    {
        ArkSteedHeapBroker *broker = self->pgoContext_.GetBroker();
        if (broker == nullptr || !stringRef.IsString()) {
            return false;
        }
        uint32_t length = 0;
        return broker->TryGetStringLength(stringRef, &length) && length == 0;
    }

    bool IsEmptyStringConstant(ValueVertex *value) const
    {
        if (std::optional<ArkSteedHeapRef> stringRef = TryGetConstantHeapRef(value)) {
            return IsEmptyStringRef(*stringRef);
        }

        auto *call = value->TryCast<CallCommonStubVertex>();
        if (call == nullptr || call->GetCommonStubID() != static_cast<uint32_t>(CommonStubID::GetStringFromConstPool)) {
            return false;
        }

        auto *stringId = call->GetInput(2)->TryCast<Int32ConstantVertex>();
        if (stringId == nullptr) {
            return false;
        }

        int32_t constDataId = stringId->GetValue();
        if (constDataId < 0 || constDataId > std::numeric_limits<uint16_t>::max()) {
            return false;
        }
        std::optional<ArkSteedNameRef> stringRef = TryGetNameRefFromConstDataId(static_cast<uint16_t>(constDataId));
        if (!stringRef.has_value()) {
            return false;
        }
        return IsEmptyStringRef(*stringRef);
    }

    std::optional<ArkSteedNameRef> TryGetNameRefFromConstDataId(uint16_t constDataId) const
    {
        if (self->preproc_->GetEnv() == nullptr || self->preproc_->GetEnv()->GetMethodLiteral() == nullptr) {
            return std::nullopt;
        }

        ArkSteedHeapBroker *broker = self->pgoContext_.GetBroker();
        if (broker == nullptr) {
            return std::nullopt;
        }

        ArkSteedNameRef name;
        if (!broker->TryGetNameFromConstantPool(constDataId, &name)) {
            return std::nullopt;
        }
        return name;
    }

    std::optional<PropertyLookupResult> TryLookupPropertyInPGOHClass(JSHClass *hclass,
                                                                     const ArkSteedNameRef &nameRef) const
    {
        auto *broker = self->pgoContext_.GetBroker();
        return broker == nullptr ? std::nullopt : broker->TryLookupProperty(hclass, nameRef);
    }

    std::optional<JSHClass *> TryResolveHClassRef(const ArkSteedHClassRef &hclassRef) const
    {
        auto *broker = self->pgoContext_.GetBroker();
        auto *hclass = broker == nullptr ? nullptr : broker->TryGetHClass(hclassRef);
        return hclass == nullptr ? std::nullopt : std::optional<JSHClass *>(hclass);
    }

    static bool IsSupportedNamedLoadAccessInfo(const PropertyAccessInfo &accessInfo)
    {
        if (accessInfo.IsTypedArrayLength()) {
            return accessInfo.mode == AccessMode::NAMED_LOAD && accessInfo.expectedHClass.IsSafeForCompile() &&
                !accessInfo.holderIsReceiver && accessInfo.hasProtoCell &&
                accessInfo.dependencies.canAssumeStableProtoChain;
        }
        if (accessInfo.mode != AccessMode::NAMED_LOAD || !accessInfo.IsDataField() ||
            (accessInfo.fieldRepresentation != AccessFieldRepresentation::TAGGED &&
             accessInfo.fieldRepresentation != AccessFieldRepresentation::UINT32) ||
            (accessInfo.fieldStorage != AccessFieldStorage::IN_OBJECT &&
             accessInfo.fieldStorage != AccessFieldStorage::PROPERTIES_ARRAY) ||
            !accessInfo.expectedHClass.IsSafeForCompile()) {
            return false;
        }
        return true;
    }

    std::optional<PropertyLookupResult> TryMakePropertyLookupResultFromAccessInfo(const PropertyAccessInfo &accessInfo,
                                                                                  JSHClass *holderHClass,
                                                                                  const ArkSteedNameRef &nameRef) const
    {
        std::optional<PropertyLookupResult> maybePlr = TryLookupPropertyInPGOHClass(holderHClass, nameRef);
        if (!maybePlr.has_value()) {
            return std::nullopt;
        }
        PropertyLookupResult plr = maybePlr.value();
        bool hasSameStorage = (accessInfo.fieldStorage == AccessFieldStorage::IN_OBJECT && plr.IsInlinedProps() &&
                               plr.GetOffset() == static_cast<uint32_t>(accessInfo.fieldOffset)) ||
                              (accessInfo.fieldStorage == AccessFieldStorage::PROPERTIES_ARRAY &&
                               !plr.IsInlinedProps() && plr.GetOffset() == accessInfo.fieldIndex);
        if (!plr.IsFound() || !plr.IsLocal() || plr.IsAccessor() || plr.IsFunction() || plr.IsLoadFromIterResult() ||
            plr.GetRepresentation() != Representation::TAGGED || !hasSameStorage) {
            return std::nullopt;
        }
        return plr;
    }

    std::optional<PropertyLookupResult> TryMakePropertyLookupResultFromAccessInfo(const PropertyAccessInfo &accessInfo,
                                                                                  JSHClass *holderHClass,
                                                                                  uint16_t constDataId) const
    {
        std::optional<ArkSteedNameRef> nameRef = TryGetNameRefFromConstDataId(constDataId);
        if (!nameRef.has_value()) {
            return std::nullopt;
        }
        return TryMakePropertyLookupResultFromAccessInfo(accessInfo, holderHClass, nameRef.value());
    }

    NamedLoadAccessInfoOpt TryConvertNamedLoadAccessInfo(const PropertyAccessInfo &accessInfo,
                                                         const ArkSteedNameRef &nameRef) const
    {
        if (!IsSupportedNamedLoadAccessInfo(accessInfo)) {
            return std::nullopt;
        }
        ArkSteedHeapBroker *broker = self->pgoContext_.GetBroker();
        if (broker == nullptr) {
            return std::nullopt;
        }
        if (accessInfo.IsTypedArrayLength()) {
            auto receiverHClass = TryResolveHClassRef(accessInfo.expectedHClass);
            if (!receiverHClass.has_value() || !IsFixedLengthTypedArrayType(receiverHClass.value()->GetObjectType())) {
                return std::nullopt;
            }
            NamedLoadAccessInfo result;
            result.receiverHClass = receiverHClass.value();
            result.lookupStartObjectHClasses = {receiverHClass.value()};
            result.lookupStartObjectHClassRefs = {accessInfo.expectedHClass};
            result.typedArrayLengthType = receiverHClass.value()->GetObjectType();
            result.typedArrayOnHeapMode = receiverHClass.value()->IsOnHeapFromBitField() ?
                OnHeapMode::ON_HEAP : OnHeapMode::NOT_ON_HEAP;
            result.hasStableProtoChain = true;
            return result;
        }
        ArkSteedObjectRef holderRef {};
        uint32_t holderHandleIndex = JitCompilationEnv::INVALID_HEAP_CONSTANT_INDEX;
        if (!accessInfo.holderIsReceiver) {
            if (!accessInfo.holder.IsSafeForCompile()) {
                return std::nullopt;
            }
            std::optional<uint32_t> recordedHolderHandleIndex = GetHeapConstantHandleIndex(accessInfo.holder);
            if (!recordedHolderHandleIndex.has_value()) {
                return std::nullopt;
            }
            holderRef = accessInfo.holder;
            holderHandleIndex = recordedHolderHandleIndex.value();
        }

        std::optional<JSHClass *> receiverHClass = TryResolveHClassRef(accessInfo.expectedHClass);
        if (!receiverHClass.has_value()) {
            return std::nullopt;
        }
        JSHClass *holderHClass = receiverHClass.value();
        uint32_t holderDepth = 0;
        std::vector<JSHClass *> expectedPrototypeHClasses;
        std::vector<ArkSteedHClassRef> expectedPrototypeHClassRefs;
        if (!accessInfo.holderIsReceiver) {
            std::optional<JSHClass *> holder = TryResolveHClassRef(accessInfo.fieldOwnerHClass);
            if (!holder.has_value()) {
                return std::nullopt;
            }
            holderHClass = holder.value();
            if (!broker->TryGetPrototypeHClasses(accessInfo.expectedHClass, accessInfo.fieldOwnerHClass,
                                                 &expectedPrototypeHClassRefs)) {
                return std::nullopt;
            }
            for (const auto &ref : expectedPrototypeHClassRefs) {
                auto hclass = TryResolveHClassRef(ref);
                if (!hclass.has_value()) {
                    return std::nullopt;
                }
                expectedPrototypeHClasses.push_back(*hclass);
            }
            holderDepth = expectedPrototypeHClasses.size();
        }
        bool isJSArrayLength = accessInfo.fieldRepresentation == AccessFieldRepresentation::UINT32;
        if (isJSArrayLength && (!accessInfo.holderIsReceiver || !holderHClass->IsJSArray() ||
            accessInfo.fieldStorage != AccessFieldStorage::IN_OBJECT ||
            accessInfo.fieldOffset != JSArray::LENGTH_OFFSET)) {
            return std::nullopt;
        }
        std::optional<PropertyLookupResult> plr = isJSArrayLength ? PropertyLookupResult() :
            TryMakePropertyLookupResultFromAccessInfo(accessInfo, holderHClass, nameRef);
        if (!plr.has_value()) {
            return std::nullopt;
        }
        bool hasStableProtoChain = holderDepth > 0 && accessInfo.dependencies.canAssumeStableProtoChain;
        NamedLoadAccessInfo result {
            .receiverHClass = receiverHClass.value(),
            .holderHClass = holderHClass,
            .holderRef = holderRef,
            .holderHandleIndex = holderHandleIndex,
            .lookupStartObjectHClasses = {receiverHClass.value()},
            .lookupStartObjectHClassRefs = {accessInfo.expectedHClass},
            .expectedPrototypeHClasses = std::move(expectedPrototypeHClasses),
            .expectedPrototypeHClassRefs = std::move(expectedPrototypeHClassRefs),
            .plr = plr.value(),
            .holderDepth = holderDepth,
            .isConst = false,
            .isJSArrayLength = isJSArrayLength,
            .canAssumeStableHClasses = accessInfo.dependencies.canAssumeStableHClass,
            .hasStableProtoChain = hasStableProtoChain,
        };
        return result;
    }

    NamedLoadAccessInfoOpt TryConvertNamedLoadAccessInfo(const PropertyAccessInfo &accessInfo,
                                                         uint16_t constDataId) const
    {
        std::optional<ArkSteedNameRef> nameRef = TryGetNameRefFromConstDataId(constDataId);
        if (!nameRef.has_value()) {
            return std::nullopt;
        }
        return TryConvertNamedLoadAccessInfo(accessInfo, nameRef.value());
    }

    static bool HasSameLoadFieldAccess(const NamedLoadAccessInfo &lhs, const NamedLoadAccessInfo &rhs)
    {
        if (lhs.isJSArrayLength != rhs.isJSArrayLength ||
            lhs.typedArrayLengthType != rhs.typedArrayLengthType ||
            lhs.typedArrayOnHeapMode != rhs.typedArrayOnHeapMode ||
            lhs.plr.GetData() != rhs.plr.GetData() || lhs.isConst != rhs.isConst ||
            lhs.holderDepth != rhs.holderDepth) {
            return false;
        }
        if (lhs.holderDepth == 0) {
            return true;
        }
        return lhs.holderHClass == rhs.holderHClass && lhs.holderHandleIndex == rhs.holderHandleIndex &&
               lhs.expectedPrototypeHClasses == rhs.expectedPrototypeHClasses;
    }

    static bool AppendHClassIfMissing(std::vector<JSHClass *> *hclasses, std::vector<ArkSteedHClassRef> *hclassRefs,
                                      JSHClass *hclass, const ArkSteedHClassRef &hclassRef)
    {
        ASSERT(hclasses != nullptr && hclassRefs != nullptr);
        ASSERT(hclasses->size() == hclassRefs->size());
        if (hclass == nullptr || !hclassRef.IsSafeForCompile()) {
            return false;
        }
        if (std::any_of(hclassRefs->begin(), hclassRefs->end(),
                        [&hclassRef](const ArkSteedHClassRef &existing) { return existing.Equals(hclassRef); })) {
            return true;
        }
        hclasses->push_back(hclass);
        hclassRefs->push_back(hclassRef);
        return true;
    }

    NamedLoadAccessInfosOpt TryGetLoadObjByNameAccessInfos(const PropertyAccessSet &accessSet,
                                                           const ArkSteedNameRef &nameRef) const
    {
        std::vector<NamedLoadAccessInfo> result;
        for (uint32_t i = 0; i < accessSet.caseCount && i < accessSet.cases.size(); ++i) {
            NamedLoadAccessInfoOpt accessInfo = TryConvertNamedLoadAccessInfo(accessSet.cases[i], nameRef);
            if (!accessInfo.has_value()) {
                return std::nullopt;
            }

            bool merged = false;
            for (NamedLoadAccessInfo &existing : result) {
                if (!HasSameLoadFieldAccess(existing, accessInfo.value())) {
                    continue;
                }
                ASSERT(accessInfo->lookupStartObjectHClasses.size() == accessInfo->lookupStartObjectHClassRefs.size());
                for (uint32_t i = 0; i < accessInfo->lookupStartObjectHClasses.size(); ++i) {
                    if (!AppendHClassIfMissing(
                            &existing.lookupStartObjectHClasses, &existing.lookupStartObjectHClassRefs,
                            accessInfo->lookupStartObjectHClasses[i], accessInfo->lookupStartObjectHClassRefs[i])) {
                        return std::nullopt;
                    }
                }
                existing.canAssumeStableHClasses =
                    existing.canAssumeStableHClasses && accessInfo->canAssumeStableHClasses;
                existing.hasStableProtoChain = existing.hasStableProtoChain && accessInfo->hasStableProtoChain;
                merged = true;
                break;
            }
            if (!merged) {
                result.push_back(std::move(accessInfo.value()));
            }
        }
        if (result.empty()) {
            return std::nullopt;
        }
        return result;
    }

    NamedLoadAccessInfosOpt TryGetLoadObjByNameAccessInfos(const PropertyAccessSet &accessSet,
                                                           uint16_t constDataId) const
    {
        std::optional<ArkSteedNameRef> nameRef = TryGetNameRefFromConstDataId(constDataId);
        if (!nameRef.has_value()) {
            return std::nullopt;
        }
        return TryGetLoadObjByNameAccessInfos(accessSet, nameRef.value());
    }
    std::optional<int32_t> TryGetInt32Value(ValueVertex *value) const
    {
        if (value == nullptr) {
            return std::nullopt;
        }
        if (auto *constant = value->TryCast<Int32ConstantVertex>()) {
            return constant->GetValue();
        }
        if (auto *constant = value->TryCast<TaggedConstantVertex>()) {
            JSTaggedValue tagged(constant->GetValue());
            return tagged.IsInt() ? std::optional<int32_t>(tagged.GetInt()) : std::nullopt;
        }
        return std::nullopt;
    }

    // Returns the double value of a double constant operand (a Float64ConstantVertex, or a
    // TaggedConstantVertex holding a tagged double). Int constants return nullopt so that int
    // DIV/MOD are not folded through the Float64 path.
    std::optional<double> TryGetFloat64Value(ValueVertex *value) const
    {
        if (value == nullptr) {
            return std::nullopt;
        }
        if (auto *constant = value->TryCast<Float64ConstantVertex>()) {
            return constant->GetValue();
        }
        if (auto *constant = value->TryCast<TaggedConstantVertex>()) {
            JSTaggedValue tagged(constant->GetValue());
            return tagged.IsDouble() ? std::optional<double>(tagged.GetDouble()) : std::nullopt;
        }
        return std::nullopt;
    }

    ValueVertex *BuildTaggedIntToI32(ValueVertex *value)
    {
        if (std::optional<int32_t> constant = TryGetInt32Value(value)) {
            return self->graph_->GetInt32Constant(*constant);
        }
        if (ValueVertex *alternative = compileInfoFacts_->TryGetAlternative(value, AlternativeNodes::Kind::INT32)) {
            return alternative;
        }
        ValueVertex *i32 = self->NewVertex<TaggedIntToI32Vertex>(compileInfoFacts_, currentBlock, {value});
        compileInfoFacts_->SetAlternative(value, AlternativeNodes::Kind::INT32, i32);
        return i32;
    }

    ValueVertex *BuildCheckedTaggedIntToI32(ValueVertex *value)
    {
        if (std::optional<int32_t> constant = TryGetInt32Value(value)) {
            return self->graph_->GetInt32Constant(*constant);
        }
        if (ValueVertex *alternative = compileInfoFacts_->TryGetAlternative(value, AlternativeNodes::Kind::INT32)) {
            return alternative;
        }
        std::vector<ValueVertex *> inputs {value};
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        ValueVertex *i32 = self->NewVertex<CheckedTaggedIntToI32Vertex>(
            currentBlock, inputs, self->chunk_, self->preproc_->GetBytecodeOffset(currentBcIndex));
        i32->Cast<CheckedTaggedIntToI32Vertex>()->SetEagerDeoptFrameState(std::move(deoptFrameState));
        compileInfoFacts_->EnsureType(value, NodeInfo::NodeType::INT);
        compileInfoFacts_->SetAlternative(value, AlternativeNodes::Kind::INT32, i32);
        return i32;
    }

    ValueVertex *BuildCheckedTaggedString(ValueVertex *value)
    {
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::STRING)) {
            return value;
        }
        std::vector<ValueVertex *> inputs {value};
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        ValueVertex *checked = self->NewVertex<CheckedTaggedStringVertex>(
            currentBlock, inputs, self->chunk_, self->preproc_->GetBytecodeOffset(currentBcIndex));
        checked->Cast<CheckedTaggedStringVertex>()->SetEagerDeoptFrameState(std::move(deoptFrameState));
        compileInfoFacts_->EnsureType(value, NodeInfo::NodeType::STRING);
        compileInfoFacts_->EnsureType(checked, NodeInfo::NodeType::STRING);
        return checked;
    }

    void BuildDeoptIfNotNumber(ValueVertex *value)
    {
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::NUMBER)) {
            return;
        }
        std::vector<ValueVertex *> inputs {value};
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        self->NewVertex<DeoptIfNotNumberVertex>(currentBlock, inputs, self->chunk_,
                                                self->preproc_->GetBytecodeOffset(currentBcIndex))
            ->SetEagerDeoptFrameState(std::move(deoptFrameState));
        compileInfoFacts_->EnsureType(value, NodeInfo::NodeType::NUMBER);
    }

    void BuildDeoptIfNotBoolean(ValueVertex *value)
    {
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::BOOLEAN)) {
            return;
        }
        std::vector<ValueVertex *> inputs {value};
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        self->NewVertex<DeoptIfNotBooleanVertex>(
            currentBlock, inputs, self->chunk_, self->preproc_->GetBytecodeOffset(currentBcIndex))
            ->SetEagerDeoptFrameState(std::move(deoptFrameState));
        compileInfoFacts_->EnsureType(value, NodeInfo::NodeType::BOOLEAN);
    }

    void BuildDeoptIfNotBigInt(ValueVertex *value)
    {
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::BIGINT)) {
            return;
        }
        std::vector<ValueVertex *> inputs {value};
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        self->NewVertex<DeoptIfNotBigIntVertex>(
            currentBlock, inputs, self->chunk_, self->preproc_->GetBytecodeOffset(currentBcIndex))
            ->SetEagerDeoptFrameState(std::move(deoptFrameState));
        compileInfoFacts_->EnsureType(value, NodeInfo::NodeType::BIGINT);
    }

    void BuildDeoptIfNotUndefinedOrNull(ValueVertex *value)
    {
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::NULL_OR_UNDEFINED)) {
            return;
        }
        std::vector<ValueVertex *> inputs {value};
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        self->NewVertex<DeoptIfNotUndefinedOrNullVertex>(
            currentBlock, inputs, self->chunk_, self->preproc_->GetBytecodeOffset(currentBcIndex))
            ->SetEagerDeoptFrameState(std::move(deoptFrameState));
        compileInfoFacts_->EnsureType(value, NodeInfo::NodeType::NULL_OR_UNDEFINED);
    }

    ValueVertex *BuildNumberToString(ValueVertex *value)
    {
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::STRING)) {
            return value;
        }
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::NUMBER)) {
            ValueVertex *result = RuntimeCall({value}, RTSTUB_ID(NumberToString), SideEffectKind::SAFE_CALL);
            compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::STRING);
            return result;
        }

        ValueVertex *result = BuildSelect<BranchIfTaggedStringVertex>(
            {value}, self->AccIndex(), [&]() -> ValueVertex * { return value; },
            [&]() -> ValueVertex * {
                BuildDeoptIfNotNumber(value);
                ValueVertex *numberResult = RuntimeCall({value}, RTSTUB_ID(NumberToString), SideEffectKind::SAFE_CALL);
                compileInfoFacts_->EnsureType(numberResult, NodeInfo::NodeType::STRING);
                return numberResult;
            });
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::STRING);
        return result;
    }

    ValueVertex *BuildTaggedI32Result(ValueVertex *rawResult)
    {
        ValueVertex *taggedResult = self->NewVertex<I32ToTaggedIntVertex>(compileInfoFacts_, currentBlock, {rawResult});
        compileInfoFacts_->EnsureType(taggedResult, NodeInfo::NodeType::INT);
        compileInfoFacts_->SetAlternative(taggedResult, AlternativeNodes::Kind::INT32, rawResult);
        return taggedResult;
    }

    ValueVertex *BuildI32WithOverflowTagged(BinaryOpKind kind, ValueVertex *left, ValueVertex *right)
    {
        ValueVertex *leftI32 = BuildTaggedIntToI32(left);
        ValueVertex *rightI32 = BuildTaggedIntToI32(right);
        ValueVertex *rawResult = BuildI32BinOpWithOverflow(kind, leftI32, rightI32);
        ASSERT(rawResult != nullptr);
        return BuildTaggedI32Result(rawResult);
    }

    ValueVertex *BuildI32BinOpValue(BinaryOpKind kind, ValueVertex *leftI32, ValueVertex *rightI32)
    {
        auto inputs = {leftI32, rightI32};
        switch (kind) {
            case BinaryOpKind::ADD:
                return self->NewVertex<I32AddVertex>(compileInfoFacts_, currentBlock, inputs);
            case BinaryOpKind::SUB:
                return self->NewVertex<I32SubVertex>(compileInfoFacts_, currentBlock, inputs);
            case BinaryOpKind::MUL:
                return self->NewVertex<I32MulVertex>(compileInfoFacts_, currentBlock, inputs);
            case BinaryOpKind::DIV:
                return self->NewVertex<I32DivVertex>(compileInfoFacts_, currentBlock, inputs);
            default:
                return nullptr;
        }
    }

    ValueVertex *BuildI32TaggedBinOpValue(BinaryOpKind kind, ValueVertex *leftI32, ValueVertex *rightI32)
    {
        ValueVertex *rawResult = BuildI32BinOpValue(kind, leftI32, rightI32);
        ASSERT(rawResult != nullptr);
        return BuildTaggedI32Result(rawResult);
    }

    template <class VertexT, class... Args>
    ValueVertex *BuildCheckedI32Arithmetic(std::initializer_list<ValueVertex *> operands, Args... args)
    {
        static_assert(std::is_same_v<VertexT, I32AddWithOverflowVertex> ||
                      std::is_same_v<VertexT, I32SubWithOverflowVertex> ||
                      std::is_same_v<VertexT, I32MulWithOverflowVertex> ||
                      std::is_same_v<VertexT, I32DivWithOverflowVertex> ||
                      std::is_same_v<VertexT, CheckedI32ModVertex> ||
                      std::is_same_v<VertexT, I32DivByConstWithCheckVertex> ||
                      std::is_same_v<VertexT, I32IncWithOverflowVertex> ||
                      std::is_same_v<VertexT, I32DecWithOverflowVertex>);
        CompileInfoFacts::ExpressionInputs inputs(self->chunk_);
        inputs.insert(inputs.end(), operands.begin(), operands.end());
        CompileInfoFacts::ExpressionOptions options(self->chunk_);
        CseBuildExpressionOptions(options, args...);
        uint32_t hash = CseHashExpression(OpcodeOf<VertexT>, inputs, options);
        if (ValueVertex *cached = compileInfoFacts_->FindExpression(
                hash, OpcodeOf<VertexT>, inputs, options, false)) {
            return cached;
        }

        // The dominating computation has already passed these checks for the same operands.
        // Keep its frame state; only semantic options, not bytecode offsets, identify the result.
        auto *result = self->NewVertex<VertexT>(
            currentBlock, inputs, self->chunk_, self->preproc_->GetBytecodeOffset(currentBcIndex), args...);
        result->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(currentBcIndex));
        compileInfoFacts_->AddExpression(hash, result, inputs, options, false);
        return result;
    }

    ValueVertex *BuildI32BinOpWithOverflow(BinaryOpKind kind, ValueVertex *leftI32, ValueVertex *rightI32)
    {
        switch (kind) {
            case BinaryOpKind::ADD:
                return BuildCheckedI32Arithmetic<I32AddWithOverflowVertex>({leftI32, rightI32});
            case BinaryOpKind::SUB:
                return BuildCheckedI32Arithmetic<I32SubWithOverflowVertex>({leftI32, rightI32});
            case BinaryOpKind::MUL:
                return BuildCheckedI32Arithmetic<I32MulWithOverflowVertex>({leftI32, rightI32});
            case BinaryOpKind::DIV:
                return BuildCheckedI32Arithmetic<I32DivWithOverflowVertex>({leftI32, rightI32});
            case BinaryOpKind::MOD:
                return BuildCheckedI32Arithmetic<CheckedI32ModVertex>({leftI32, rightI32});
            default:
                return nullptr;
        }
    }

    struct SignedDivisorMagic {
        int32_t magic;
        uint32_t shift;
    };

    static SignedDivisorMagic ComputeSignedDivisorMagic(int32_t divisor)
    {
        ASSERT(divisor <= -2 || divisor >= 2);
        constexpr uint32_t BIT_WIDTH = 32;
        uint64_t highOne = 1ULL << (BIT_WIDTH - 1U);
        uint64_t ad =
            divisor < 0 ? static_cast<uint64_t>(-static_cast<int64_t>(divisor)) : static_cast<uint64_t>(divisor);
        uint64_t divisorBits = static_cast<uint64_t>(static_cast<int64_t>(divisor));
        uint64_t t = highOne + (divisorBits >> 63U);
        uint64_t anc = t - 1U - t % ad;
        int64_t p = BIT_WIDTH - 1U;
        uint64_t q1 = highOne / anc;
        uint64_t r1 = highOne - q1 * anc;
        uint64_t q2 = highOne / ad;
        uint64_t r2 = highOne - q2 * ad;
        uint64_t delta = 0U;

        do {
            ++p;
            q1 *= 2U;
            r1 *= 2U;
            if (r1 >= anc) {
                ++q1;
                r1 -= anc;
            }
            q2 *= 2U;
            r2 *= 2U;
            if (r2 >= ad) {
                ++q2;
                r2 -= ad;
            }
            delta = ad - r2;
        } while (q1 < delta || (q1 == delta && r1 == 0));

        int64_t magic = static_cast<int64_t>(q2) + 1;
        if (divisor < 0) {
            magic = -magic;
        }
        return {static_cast<int32_t>(magic), static_cast<uint32_t>(p - BIT_WIDTH)};
    }

    ValueVertex *BuildI32Operand(ValueVertex *value, bool knownInt)
    {
        return knownInt ? BuildTaggedIntToI32(value) : BuildCheckedTaggedIntToI32(value);
    }

    ValueVertex *TryReuseKnownIntOperand(ValueVertex *value, bool knownInt)
    {
        if (!knownInt) {
            return nullptr;
        }
        compileInfoFacts_->EnsureType(value, NodeInfo::NodeType::INT);
        return value;
    }

    ValueVertex *BuildI32DivByConstWithCheckTagged(ValueVertex *left, bool leftKnownInt, int32_t divisor)
    {
        SignedDivisorMagic magic = ComputeSignedDivisorMagic(divisor);
        ValueVertex *leftI32 = BuildI32Operand(left, leftKnownInt);
        ValueVertex *rawResult = BuildCheckedI32Arithmetic<I32DivByConstWithCheckVertex>(
            {leftI32}, divisor, magic.magic, magic.shift);
        return BuildTaggedI32Result(rawResult);
    }

    void BuildDeoptIfInt32Condition(ValueVertex *leftI32, ValueVertex *rightI32, Condition condition,
                                    kungfu::DeoptType deoptType)
    {
        std::vector<ValueVertex *> inputs {leftI32, rightI32};
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        self->NewVertex<DeoptIfInt32ConditionVertex>(currentBlock, inputs, self->chunk_,
                                                     self->preproc_->GetBytecodeOffset(currentBcIndex), condition,
                                                     deoptType)
            ->SetEagerDeoptFrameState(std::move(deoptFrameState));
    }

    void BuildDeoptIfFloat64Condition(ValueVertex *leftF64, ValueVertex *rightF64, Condition condition,
                                      kungfu::DeoptType deoptType)
    {
        std::vector<ValueVertex *> inputs {leftF64, rightF64};
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        self->NewVertex<DeoptIfFloat64ConditionVertex>(currentBlock, inputs, self->chunk_,
                                                       self->preproc_->GetBytecodeOffset(currentBcIndex), condition,
                                                       deoptType)
            ->SetEagerDeoptFrameState(std::move(deoptFrameState));
    }

    ValueVertex *BuildTaggedIntConstant(int32_t value)
    {
        ValueVertex *constant = self->graph_->GetTaggedConstant(JSTaggedValue(value).GetRawData());
        compileInfoFacts_->EnsureType(constant, NodeInfo::NodeType::INT);
        return constant;
    }

    ValueVertex *TryBuildI32MulByZeroReduction(ValueVertex *value, bool valueKnownInt)
    {
        if (std::optional<int32_t> constant = TryGetInt32Value(value)) {
            if (*constant < 0) {
                return nullptr;
            }
            return BuildTaggedIntConstant(0);
        }

        ValueVertex *valueI32 = BuildI32Operand(value, valueKnownInt);
        BuildDeoptIfInt32Condition(valueI32, self->graph_->GetInt32Constant(0), Condition::LESS_THAN,
                                   kungfu::DeoptType::PRODUCTISNEGATIVEZERO);
        return BuildTaggedIntConstant(0);
    }

    ValueVertex *TryBuildI32DivByMinusOneReduction(ValueVertex *value, bool valueKnownInt)
    {
        std::optional<int32_t> constant = TryGetInt32Value(value);
        if (constant.has_value() && *constant == 0) {
            return nullptr;
        }

        ValueVertex *valueI32 = BuildI32Operand(value, valueKnownInt);
        ValueVertex *zeroI32 = self->graph_->GetInt32Constant(0);
        if (!constant.has_value()) {
            BuildDeoptIfInt32Condition(valueI32, zeroI32, Condition::EQUAL, kungfu::DeoptType::DIVZERO2);
        }
        ValueVertex *rawResult = BuildI32BinOpWithOverflow(BinaryOpKind::SUB, zeroI32, valueI32);
        return BuildTaggedI32Result(rawResult);
    }

    ValueVertex *TryBuildI32ModByOneReduction(ValueVertex *left, bool leftKnownInt, int32_t divisor)
    {
        ASSERT(divisor == 1 || divisor == -1);

        std::optional<int32_t> leftValue = TryGetInt32Value(left);
        if (leftValue.has_value()) {
            if (*leftValue < 0 || (divisor == -1 && *leftValue == std::numeric_limits<int32_t>::min())) {
                return nullptr;
            }
            return BuildTaggedIntConstant(0);
        }

        ValueVertex *leftI32 = BuildI32Operand(left, leftKnownInt);
        if (divisor == -1) {
            BuildDeoptIfInt32Condition(leftI32, self->graph_->GetInt32Constant(std::numeric_limits<int32_t>::min()),
                                       Condition::EQUAL, kungfu::DeoptType::INT32OVERFLOW1);
        }
        BuildDeoptIfInt32Condition(leftI32, self->graph_->GetInt32Constant(0), Condition::LESS_THAN,
                                   kungfu::DeoptType::REMAINDERISNEGATIVEZERO);
        return BuildTaggedIntConstant(0);
    }

    ValueVertex *TryBuildI32RightConstantReduction(BinaryOpKind kind, ValueVertex *left, bool leftKnownInt,
                                                   int32_t rightValue)
    {
        switch (kind) {
            case BinaryOpKind::ADD:
                if (rightValue == 1) {
                    // x + 1 -> ++x.
                    return BuildIntUnaryOp(CommonStubID::Inc, left, leftKnownInt);
                }
                if (rightValue == 0) {
                    // x + 0 -> x.
                    return TryReuseKnownIntOperand(left, leftKnownInt);
                }
                break;
            case BinaryOpKind::SUB:
                if (rightValue == 1) {
                    // x - 1 -> --x.
                    return BuildIntUnaryOp(CommonStubID::Dec, left, leftKnownInt);
                }
                if (rightValue == 0) {
                    // x - 0 -> x.
                    return TryReuseKnownIntOperand(left, leftKnownInt);
                }
                break;
            case BinaryOpKind::MUL:
                if (rightValue == 0) {
                    // x * 0 -> 0, guarding against -0.
                    return TryBuildI32MulByZeroReduction(left, leftKnownInt);
                }
                if (rightValue == 1) {
                    // x * 1 -> x.
                    return TryReuseKnownIntOperand(left, leftKnownInt);
                }
                break;
            case BinaryOpKind::DIV:
                if (rightValue == -1) {
                    // x / -1 -> -x, guarding zero and overflow.
                    return TryBuildI32DivByMinusOneReduction(left, leftKnownInt);
                }
                if (rightValue == 1) {
                    // x / 1 -> x.
                    return TryReuseKnownIntOperand(left, leftKnownInt);
                }
                if (rightValue != 0) {
                    // x / c -> checked constant-divisor path.
                    return BuildI32DivByConstWithCheckTagged(left, leftKnownInt, rightValue);
                }
                break;
            case BinaryOpKind::MOD:
                if (rightValue == 1 || rightValue == -1) {
                    // x % +/-1 -> 0, guarding -0 and overflow.
                    return TryBuildI32ModByOneReduction(left, leftKnownInt, rightValue);
                }
                break;
            default:
                break;
        }
        return nullptr;
    }

    ValueVertex *TryBuildI32LeftConstantReduction(BinaryOpKind kind, int32_t leftValue, ValueVertex *right,
                                                  bool rightKnownInt)
    {
        switch (kind) {
            case BinaryOpKind::ADD:
                if (leftValue == 1) {
                    // 1 + x -> ++x.
                    return BuildIntUnaryOp(CommonStubID::Inc, right, rightKnownInt);
                }
                if (leftValue == 0) {
                    // 0 + x -> x.
                    return TryReuseKnownIntOperand(right, rightKnownInt);
                }
                break;
            case BinaryOpKind::MUL:
                if (leftValue == 0) {
                    // 0 * x -> 0, guarding against -0.
                    return TryBuildI32MulByZeroReduction(right, rightKnownInt);
                }
                if (leftValue == 1) {
                    // 1 * x -> x.
                    return TryReuseKnownIntOperand(right, rightKnownInt);
                }
                break;
            default:
                break;
        }
        return nullptr;
    }

    ValueVertex *TryBuildI32BinaryReduction(BinaryOpKind kind, ValueVertex *left, ValueVertex *right, bool leftKnownInt,
                                            bool rightKnownInt)
    {
        if (std::optional<int32_t> rightValue = TryGetInt32Value(right)) {
            if (ValueVertex *reduced = TryBuildI32RightConstantReduction(kind, left, leftKnownInt, *rightValue)) {
                return reduced;
            }
        }

        if (std::optional<int32_t> leftValue = TryGetInt32Value(left)) {
            if (ValueVertex *reduced = TryBuildI32LeftConstantReduction(kind, *leftValue, right, rightKnownInt)) {
                return reduced;
            }
        }
        return nullptr;
    }

    ValueVertex *BuildI32BinOp(BinaryOpKind kind, ValueVertex *left, ValueVertex *right)
    {
        ASSERT(SupportsI32CheckedBinOp(kind));

        bool leftKnownInt = compileInfoFacts_->CheckType(left, NodeInfo::NodeType::INT);
        bool rightKnownInt = compileInfoFacts_->CheckType(right, NodeInfo::NodeType::INT);
        return BuildI32CheckedBinOp(kind, left, right, leftKnownInt, rightKnownInt);
    }

    ValueVertex *BuildI32CheckedBinOp(BinaryOpKind kind, ValueVertex *left, ValueVertex *right, bool leftKnownInt,
                                      bool rightKnownInt)
    {
        ASSERT(SupportsI32CheckedBinOp(kind));

        bool leftIsConst = TryGetInt32Value(left).has_value();
        bool rightIsConst = TryGetInt32Value(right).has_value();
        if (leftIsConst || rightIsConst) {
            if (ValueVertex *reduced = TryBuildI32BinaryReduction(kind, left, right, leftKnownInt, rightKnownInt)) {
                return reduced;
            }
        }

        if (leftKnownInt && rightKnownInt) {
            return BuildI32WithOverflowTagged(kind, left, right);
        }

        ValueVertex *leftI32 = leftKnownInt ? BuildTaggedIntToI32(left) : BuildCheckedTaggedIntToI32(left);
        ValueVertex *rightI32 = rightKnownInt ? BuildTaggedIntToI32(right) : BuildCheckedTaggedIntToI32(right);
        ValueVertex *rawResult = BuildI32BinOpWithOverflow(kind, leftI32, rightI32);
        ASSERT(rawResult != nullptr);
        return BuildTaggedI32Result(rawResult);
    }

    ValueVertex *TryBuildProvenIntBinOp(BinaryOpKind kind, ValueVertex *left, ValueVertex *right)
    {
        if (!SupportsI32CheckedBinOp(kind)) {
            return nullptr;
        }
        bool leftKnownInt = compileInfoFacts_->CheckType(left, NodeInfo::NodeType::INT);
        bool rightKnownInt = compileInfoFacts_->CheckType(right, NodeInfo::NodeType::INT);
        if (!leftKnownInt || !rightKnownInt) {
            return nullptr;
        }
        return BuildI32BinOp(kind, left, right);
    }

    ValueVertex *BuildCheckedNumberToF64(ValueVertex *value)
    {
        if (ValueVertex *alternative =
                compileInfoFacts_->TryGetAlternative(value, AlternativeNodes::Kind::HOLEY_FLOAT64)) {
            return alternative;
        }
        if (std::optional<int32_t> constant = TryGetInt32Value(value)) {
            return self->graph_->GetFloat64Constant(static_cast<double>(*constant));
        }
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::INT)) {
            ValueVertex *i32 = BuildTaggedIntToI32(value);
            ValueVertex *f64 = self->NewVertex<I32ToF64Vertex>(compileInfoFacts_, currentBlock,
                                                               std::initializer_list<ValueVertex *> {i32});
            compileInfoFacts_->SetAlternative(value, AlternativeNodes::Kind::HOLEY_FLOAT64, f64);
            return f64;
        }
        std::vector<ValueVertex *> inputs {value};
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        ValueVertex *f64 = self->NewVertex<CheckedNumberToF64Vertex>(currentBlock, inputs, self->chunk_,
                                                                     self->preproc_->GetBytecodeOffset(currentBcIndex));
        f64->Cast<CheckedNumberToF64Vertex>()->SetEagerDeoptFrameState(std::move(deoptFrameState));
        compileInfoFacts_->EnsureType(value, NodeInfo::NodeType::NUMBER);
        compileInfoFacts_->SetAlternative(value, AlternativeNodes::Kind::HOLEY_FLOAT64, f64);
        return f64;
    }

    ValueVertex *BuildF64BinOpValue(BinaryOpKind kind, ValueVertex *leftF64, ValueVertex *rightF64)
    {
        auto inputs = {leftF64, rightF64};
        switch (kind) {
            case BinaryOpKind::ADD:
                return self->NewVertex<F64AddVertex>(compileInfoFacts_, currentBlock, inputs);
            case BinaryOpKind::SUB:
                return self->NewVertex<F64SubVertex>(compileInfoFacts_, currentBlock, inputs);
            case BinaryOpKind::MUL:
                return self->NewVertex<F64MulVertex>(compileInfoFacts_, currentBlock, inputs);
            case BinaryOpKind::DIV:
                return self->NewVertex<F64DivVertex>(compileInfoFacts_, currentBlock, inputs);
            default:
                return nullptr;
        }
    }

    bool IsPositiveZero(ValueVertex *value) const
    {
        auto *constant = value->TryCast<Float64ConstantVertex>();
        return constant != nullptr && constant->GetValue() == 0.0 && !std::signbit(constant->GetValue());
    }

    bool IsNegativeZero(ValueVertex *value) const
    {
        auto *constant = value->TryCast<Float64ConstantVertex>();
        return constant != nullptr && constant->GetValue() == 0.0 && std::signbit(constant->GetValue());
    }

    bool IsFloat64One(ValueVertex *value) const
    {
        auto *constant = value->TryCast<Float64ConstantVertex>();
        return constant != nullptr && constant->GetValue() == 1.0;
    }

    ValueVertex *BuildTaggedF64Value(ValueVertex *rawResult)
    {
        ValueVertex *taggedResult =
            self->NewVertex<F64ToTaggedDoubleVertex>(compileInfoFacts_, currentBlock, {rawResult});
        compileInfoFacts_->EnsureType(taggedResult, NodeInfo::NodeType::DOUBLE);
        compileInfoFacts_->SetAlternative(taggedResult, AlternativeNodes::Kind::HOLEY_FLOAT64, rawResult);
        return taggedResult;
    }

    ValueVertex *TryBuildF64Identity(BinaryOpKind kind, ValueVertex *leftF64, ValueVertex *rightF64)
    {
        switch (kind) {
            case BinaryOpKind::ADD:
                if (IsNegativeZero(rightF64)) {
                    return BuildTaggedF64Value(leftF64);
                }
                break;
            case BinaryOpKind::SUB:
                if (IsPositiveZero(rightF64)) {
                    return BuildTaggedF64Value(leftF64);
                }
                break;
            case BinaryOpKind::MUL:
            case BinaryOpKind::DIV:
                if (IsFloat64One(rightF64)) {
                    return BuildTaggedF64Value(leftF64);
                }
                break;
            default:
                break;
        }
        return nullptr;
    }

    ValueVertex *BuildF64TaggedBinOpValue(BinaryOpKind kind, ValueVertex *leftF64, ValueVertex *rightF64)
    {
        if (ValueVertex *identity = TryBuildF64Identity(kind, leftF64, rightF64)) {
            return identity;
        }
        ValueVertex *rawResult = BuildF64BinOpValue(kind, leftF64, rightF64);
        if (rawResult == nullptr) {
            return nullptr;
        }
        return BuildTaggedF64Value(rawResult);
    }

    ValueVertex *BuildF64NumberBinOp(BinaryOpKind kind, ValueVertex *left, ValueVertex *right)
    {
        ASSERT(SupportsF64BinOp(kind));
        ValueVertex *leftF64 = BuildCheckedNumberToF64(left);
        ValueVertex *rightF64 = BuildCheckedNumberToF64(right);
        ValueVertex *taggedResult = BuildF64TaggedBinOpValue(kind, leftF64, rightF64);
        ASSERT(taggedResult != nullptr);
        return taggedResult;
    }

    ValueVertex *BuildCheckedNonNegativeI32ToTaggedInt(ValueVertex *rawResult)
    {
        std::vector<ValueVertex *> inputs {rawResult};
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        ValueVertex *tagged = self->NewVertex<CheckedNonNegativeI32ToTaggedIntVertex>(
            currentBlock, inputs, self->chunk_, self->preproc_->GetBytecodeOffset(currentBcIndex));
        tagged->Cast<CheckedNonNegativeI32ToTaggedIntVertex>()->SetEagerDeoptFrameState(std::move(deoptFrameState));
        compileInfoFacts_->EnsureType(tagged, NodeInfo::NodeType::INT);
        compileInfoFacts_->SetAlternative(tagged, AlternativeNodes::Kind::INT32, rawResult);
        return tagged;
    }

    ValueVertex *BuildGenericBitwiseBinOp(IntBitwiseKind kind, ValueVertex *left, ValueVertex *right)
    {
        switch (kind) {
            case IntBitwiseKind::BITWISE_AND:
                return CommonStubCall({glue, left, right, GlobalEnv()}, CommonStubID::And);
            case IntBitwiseKind::BITWISE_OR:
                return CommonStubCall({glue, left, right, GlobalEnv()}, CommonStubID::Or);
            case IntBitwiseKind::BITWISE_XOR:
                return CommonStubCall({glue, left, right, GlobalEnv()}, CommonStubID::Xor);
            case IntBitwiseKind::SHIFT_LEFT:
                return CommonStubCall({glue, left, right, GlobalEnv()}, CommonStubID::Shl);
            case IntBitwiseKind::SHIFT_RIGHT_LOGICAL:
                return CommonStubCall({glue, left, right, GlobalEnv()}, CommonStubID::Shr);
            case IntBitwiseKind::SHIFT_RIGHT_ARITHMETIC:
                return CommonStubCall({glue, left, right, GlobalEnv()}, CommonStubID::Ashr);
        }
        UNREACHABLE();
    }

    ValueVertex *TryBuildI32BitwiseReduction(IntBitwiseKind kind, ValueVertex *left, ValueVertex *right)
    {
        std::optional<int32_t> leftValue = TryGetInt32Value(left);
        std::optional<int32_t> rightValue = TryGetInt32Value(right);
        auto knownInt = [this](ValueVertex *value) -> ValueVertex * {
            compileInfoFacts_->EnsureType(value, NodeInfo::NodeType::INT);
            return value;
        };
        auto taggedIntConstant = [this](int32_t value) -> ValueVertex * {
            ValueVertex *constant = self->graph_->GetTaggedConstant(JSTaggedValue(value).GetRawData());
            compileInfoFacts_->EnsureType(constant, NodeInfo::NodeType::INT);
            return constant;
        };

        if (leftValue.has_value() && rightValue.has_value()) {
            int32_t lhs = *leftValue;
            uint32_t shift = static_cast<uint32_t>(*rightValue) & 31U;
            switch (kind) {
                case IntBitwiseKind::BITWISE_AND:
                    return taggedIntConstant(lhs & *rightValue);
                case IntBitwiseKind::BITWISE_OR:
                    return taggedIntConstant(lhs | *rightValue);
                case IntBitwiseKind::BITWISE_XOR:
                    return taggedIntConstant(lhs ^ *rightValue);
                case IntBitwiseKind::SHIFT_LEFT:
                    return taggedIntConstant(static_cast<int32_t>(static_cast<uint32_t>(lhs) << shift));
                case IntBitwiseKind::SHIFT_RIGHT_ARITHMETIC:
                    return taggedIntConstant(lhs >> static_cast<int32_t>(shift));
                case IntBitwiseKind::SHIFT_RIGHT_LOGICAL: {
                    // >>> yields a uint32; fold only when it fits int32 (non-negative), otherwise
                    // leave it so the SHR tagging path (CheckedNonNegativeI32ToTaggedInt) deopts
                    // to double as JS requires (e.g. (-1) >>> 0 === 4294967295).
                    uint32_t ur = static_cast<uint32_t>(lhs) >> shift;
                    if (ur > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                        break;
                    }
                    return taggedIntConstant(static_cast<int32_t>(ur));
                }
                default:
                    break;
            }
        }

        if (rightValue.has_value()) {
            switch (kind) {
                case IntBitwiseKind::BITWISE_AND:
                    if (*rightValue == -1) {
                        return knownInt(left);
                    }
                    if (*rightValue == 0) {
                        return taggedIntConstant(0);
                    }
                    break;
                case IntBitwiseKind::BITWISE_OR:
                    if (*rightValue == 0) {
                        return knownInt(left);
                    }
                    if (*rightValue == -1) {
                        return taggedIntConstant(-1);
                    }
                    break;
                case IntBitwiseKind::BITWISE_XOR:
                    if (*rightValue == 0) {
                        return knownInt(left);
                    }
                    break;
                case IntBitwiseKind::SHIFT_LEFT:
                case IntBitwiseKind::SHIFT_RIGHT_ARITHMETIC:
                    if ((static_cast<uint32_t>(*rightValue) & 31U) == 0) {
                        return knownInt(left);
                    }
                    break;
                case IntBitwiseKind::SHIFT_RIGHT_LOGICAL:
                    break;
            }
        }

        if (leftValue.has_value()) {
            switch (kind) {
                case IntBitwiseKind::BITWISE_AND:
                    if (*leftValue == -1) {
                        return knownInt(right);
                    }
                    if (*leftValue == 0) {
                        return taggedIntConstant(0);
                    }
                    break;
                case IntBitwiseKind::BITWISE_OR:
                    if (*leftValue == 0) {
                        return knownInt(right);
                    }
                    if (*leftValue == -1) {
                        return taggedIntConstant(-1);
                    }
                    break;
                case IntBitwiseKind::BITWISE_XOR:
                    if (*leftValue == 0) {
                        return knownInt(right);
                    }
                    break;
                default:
                    break;
            }
        }

        return nullptr;
    }

    ValueVertex *BuildI32BitwiseTaggedValue(IntBitwiseKind kind, ValueVertex *leftI32, ValueVertex *rightI32)
    {
        ValueVertex *raw =
            self->NewVertex<I32BitwiseBinaryVertex>(compileInfoFacts_, currentBlock, {leftI32, rightI32}, kind);
        if (kind != IntBitwiseKind::SHIFT_RIGHT_LOGICAL) {
            return BuildTaggedI32Result(raw);
        }
        return BuildCheckedNonNegativeI32ToTaggedInt(raw);
    }

    ValueVertex *BuildI32BitwiseBinOp(IntBitwiseKind kind, ValueVertex *left, ValueVertex *right, bool leftKnownInt,
                                      bool rightKnownInt)
    {
        if (leftKnownInt && rightKnownInt) {
            if (ValueVertex *reduced = TryBuildI32BitwiseReduction(kind, left, right)) {
                return reduced;
            }
        }

        ValueVertex *leftI32 = leftKnownInt ? BuildTaggedIntToI32(left) : BuildCheckedTaggedIntToI32(left);
        ValueVertex *rightI32 = rightKnownInt ? BuildTaggedIntToI32(right) : BuildCheckedTaggedIntToI32(right);
        return BuildI32BitwiseTaggedValue(kind, leftI32, rightI32);
    }

    ValueVertex *BuildTruncatingNumberToInt32(ValueVertex *value)
    {
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::INT)) {
            return BuildTaggedIntToI32(value);
        }
        ValueVertex *valueF64 = BuildCheckedNumberToF64(value);
        return self->NewVertex<F64ToI32TruncVertex>(compileInfoFacts_, currentBlock, {valueF64});
    }

    void BuildTruncatingNumberTypedArrayStore(ValueVertex *receiver, ValueVertex *index, ValueVertex *value,
                                              const ElementStoreAccessInfo &access)
    {
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::INT)) {
            ValueVertex *intValue = BuildTaggedIntToI32(value);
            self->NewVertex<StoreIntTypedArrayElementVertex>(
                compileInfoFacts_, currentBlock, {receiver, index, intValue}, access.typedArrayType, access.onHeapMode);
            return;
        }

        ValueVertex *doubleValue = BuildCheckedNumberToF64(value);
        ValueVertex *minInt64 = self->graph_->GetFloat64Constant(-0x1p63);
        ValueVertex *maxInt64 = self->graph_->GetFloat64Constant(0x1p63);
        BB *checkUpperBlock = self->NewBlock();
        BB *fastBlock = self->NewBlock();
        BB *lowerOverflowBlock = self->NewBlock();
        BB *upperOverflowBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();

        self->FinishBlockWithBranch<BranchIfFloat64CompareVertex>(currentBlock, {doubleValue, minInt64},
                                                                  checkUpperBlock, lowerOverflowBlock,
                                                                  Condition::GREATER_THAN_OR_EQUAL);

        currentBlock = checkUpperBlock;
        self->FinishBlockWithBranch<BranchIfFloat64CompareVertex>(currentBlock, {doubleValue, maxInt64}, fastBlock,
                                                                  upperOverflowBlock, Condition::LESS_THAN);

        currentBlock = fastBlock;
        ValueVertex *fastValue = self->NewVertex<F64ToI32TruncVertex>(
            compileInfoFacts_, currentBlock, std::initializer_list<ValueVertex *> {doubleValue});
        self->NewVertex<StoreIntTypedArrayElementVertex>(compileInfoFacts_, currentBlock, {receiver, index, fastValue},
                                                         access.typedArrayType, access.onHeapMode);
        self->FinishBlockWithJump(currentBlock, doneBlock);

        for (BB *overflowBlock : {lowerOverflowBlock, upperOverflowBlock}) {
            currentBlock = overflowBlock;
            currentBlock->SetDeferred(true);
            ValueVertex *slowValue = self->NewVertex<DoubleToInt32CallVertex>(
                compileInfoFacts_, currentBlock, std::initializer_list<ValueVertex *> {doubleValue});
            self->NewVertex<StoreIntTypedArrayElementVertex>(compileInfoFacts_, currentBlock,
                                                             {receiver, index, slowValue}, access.typedArrayType,
                                                             access.onHeapMode);
            self->FinishBlockWithJump(currentBlock, doneBlock);
        }

        currentBlock = doneBlock;
    }

    void BuildClampedUint8TypedArrayStore(ValueVertex *receiver, ValueVertex *index, ValueVertex *value,
                                          const ElementStoreAccessInfo &access)
    {
        ValueVertex *clampedValue = nullptr;
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::INT)) {
            ValueVertex *intValue = BuildTaggedIntToI32(value);
            clampedValue = self->NewVertex<I32ToUint8ClampedVertex>(compileInfoFacts_, currentBlock,
                                                                    std::initializer_list<ValueVertex *> {intValue});
        } else {
            ValueVertex *doubleValue = BuildCheckedNumberToF64(value);
            clampedValue = self->NewVertex<F64ToUint8ClampedVertex>(compileInfoFacts_, currentBlock,
                                                                    std::initializer_list<ValueVertex *> {doubleValue});
        }
        self->NewVertex<StoreIntTypedArrayElementVertex>(
            compileInfoFacts_, currentBlock, {receiver, index, clampedValue}, access.typedArrayType, access.onHeapMode);
    }

    ValueVertex *BuildBitwiseOperation(IntBitwiseKind kind)
    {
        ValueVertex *left = LoadRegister(currentBcInfo, 0);
        ValueVertex *right = frameState.GetAcc();
        bool leftKnownInt = compileInfoFacts_->CheckType(left, NodeInfo::NodeType::INT);
        bool rightKnownInt = compileInfoFacts_->CheckType(right, NodeInfo::NodeType::INT);
        if (leftKnownInt && rightKnownInt) {
            return BuildI32BitwiseBinOp(kind, left, right, leftKnownInt, rightKnownInt);
        }

        OperationFeedback feedback = self->pgoContext_.ReadOperationFeedback(currentBcInfo);
        if (feedback.hint == ArkSteedOperationHint::INT) {
            LogOperationFeedback(feedback, "Bitwise", "I32BitwiseBinOp");
            return BuildI32BitwiseBinOp(kind, left, right, false, false);
        }
        if (feedback.hint == ArkSteedOperationHint::NUMBER) {
            LogOperationFeedback(feedback, "Bitwise", "TruncatingI32BitwiseBinOp");
            ValueVertex *leftI32 = BuildTruncatingNumberToInt32(left);
            ValueVertex *rightI32 = BuildTruncatingNumberToInt32(right);
            return BuildI32BitwiseTaggedValue(kind, leftI32, rightI32);
        }
        LogOperationFeedback(feedback, "Bitwise", "GenericBitwiseBinOp");
        return BuildGenericBitwiseBinOp(kind, left, right);
    }

    ValueVertex *BuildStringAdd(ValueVertex *left, ValueVertex *right)
    {
        ValueVertex *result = CommonStubCall(
            {glue, left, right, self->graph_->GetInt32Constant(0), GlobalEnv()}, CommonStubID::StringAdd,
            SideEffectKind::SAFE_CALL);
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::STRING);
        return result;
    }

    ValueVertex *TryBuildStringAdd(ValueVertex *left, ValueVertex *right, const OperationFeedback &feedback)
    {
        bool leftKnownString = compileInfoFacts_->CheckType(left, NodeInfo::NodeType::STRING);
        bool rightKnownString = compileInfoFacts_->CheckType(right, NodeInfo::NodeType::STRING);

        if (leftKnownString && rightKnownString) {
            if (IsEmptyStringConstant(left)) {
                return right;
            }
            if (IsEmptyStringConstant(right)) {
                return left;
            }
            return BuildStringAdd(left, right);
        }

        switch (feedback.hint) {
            case ArkSteedOperationHint::STRING: {
                LogOperationFeedback(feedback, "BinaryStringAdd", "StringAdd");
                ValueVertex *checkedLeft = leftKnownString ? left : BuildCheckedTaggedString(left);
                ValueVertex *checkedRight = rightKnownString ? right : BuildCheckedTaggedString(right);
                if (leftKnownString && IsEmptyStringConstant(left)) {
                    return checkedRight;
                }
                if (rightKnownString && IsEmptyStringConstant(right)) {
                    return checkedLeft;
                }
                return BuildStringAdd(checkedLeft, checkedRight);
            }

            case ArkSteedOperationHint::NUMBER_OR_STRING: {
                LogOperationFeedback(feedback, "BinaryStringAdd", "StringAdd");
                if (leftKnownString) {
                    if (IsEmptyStringConstant(left)) {
                        return BuildNumberToString(right);
                    }
                    return BuildStringAdd(left, BuildNumberToString(right));
                }
                if (rightKnownString) {
                    if (IsEmptyStringConstant(right)) {
                        return BuildNumberToString(left);
                    }
                    return BuildStringAdd(BuildNumberToString(left), right);
                }
                return nullptr;
            }

            default:
                return nullptr;
        }
    }

    ValueVertex *GetBooleanConstant(bool value)
    {
        JSTaggedValue tagged(value ? JSTaggedValue::VALUE_TRUE : JSTaggedValue::VALUE_FALSE);
        return TaggedConstantFromFoldedValue(tagged);
    }

    ValueVertex *TryBuildKnownIntToBoolean(ValueVertex *value, bool trueIfNonZero)
    {
        if (!compileInfoFacts_->CheckType(value, NodeInfo::NodeType::INT)) {
            return nullptr;
        }
        return BuildI32ToBoolean(BuildTaggedIntToI32(value), trueIfNonZero);
    }

    ValueVertex *BuildI32ToBoolean(ValueVertex *i32, bool trueIfNonZero)
    {
        ValueVertex *zero = self->graph_->GetInt32Constant(0);
        Condition condition = trueIfNonZero ? Condition::NOT_EQUAL : Condition::EQUAL;
        ValueVertex *result = self->NewVertex<I32ConditionCheckVertex>(
            compileInfoFacts_, currentBlock, {i32, zero}, condition);
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
        return result;
    }

    ValueVertex *BuildGenericCompareOp(JSCondition kind, ValueVertex *left, ValueVertex *right)
    {
        CommonStubID stubId;
        switch (kind) {
            case JSCondition::EQUAL:
                stubId = CommonStubID::Equal;
                break;
            case JSCondition::NOT_EQUAL:
                stubId = CommonStubID::NotEqual;
                break;
            case JSCondition::LESS_THAN:
                stubId = CommonStubID::Less;
                break;
            case JSCondition::LESS_THAN_OR_EQUAL:
                stubId = CommonStubID::LessEq;
                break;
            case JSCondition::GREATER_THAN:
                stubId = CommonStubID::Greater;
                break;
            case JSCondition::GREATER_THAN_OR_EQUAL:
                stubId = CommonStubID::GreaterEq;
                break;
            case JSCondition::STRICT_EQUAL:
                stubId = CommonStubID::StrictEqual;
                break;
            case JSCondition::STRICT_NOT_EQUAL:
                stubId = CommonStubID::StrictNotEqual;
                break;
            default:
                UNREACHABLE();
        }
        SideEffectKind sideEffectKind = IsStrictEqualityCompare(kind)
            ? SideEffectKind::SAFE_CALL : SideEffectKind::UNKNOWN_CALL;
        ValueVertex *result = CommonStubCall({glue, left, right, GlobalEnv()}, stubId, sideEffectKind);
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
        return result;
    }

    ValueVertex *BuildTaggedEqual(ValueVertex *left, ValueVertex *right)
    {
        ValueVertex *result = self->NewVertex<TaggedEqualVertex>(compileInfoFacts_, currentBlock, {left, right});
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
        return result;
    }

    ValueVertex *BuildTaggedNotEqual(ValueVertex *left, ValueVertex *right)
    {
        ValueVertex *result = self->NewVertex<TaggedNotEqualVertex>(compileInfoFacts_, currentBlock, {left, right});
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
        return result;
    }

    ValueVertex *TryReduceLooseEqualityAgainstConstant(JSCondition kind, ValueVertex *left, ValueVertex *right)
    {
        // Note: Only nullish cases are implemented currently.
        bool leftNullish = IsNullishRootValue(left);
        bool rightNullish = IsNullishRootValue(right);
        if (leftNullish && rightNullish) {
            // Every nullish-root pair is loosely equal to the other.
            return GetBooleanConstant(IsPositiveEqualityCompare(kind));
        }
        // Loose equality against a nullish root: `x == null` (and `x == undefined`)
        // The check reduces to `(x | 1) === null` (based on the fact that VALUE_UNDEFINED=0x2, VALUE_NULL=0x3)
        if (leftNullish || rightNullish) {
            ValueVertex *value = leftNullish ? right : left;
            ValueVertex *raw = self->NewVertex<TaggedToRawI64Vertex>(compileInfoFacts_, currentBlock, {value});
            ValueVertex *one = self->graph_->GetInt64Constant(1);
            ValueVertex *normalizedRaw = self->NewVertex<I64BitwiseBinaryVertex>(
                compileInfoFacts_, currentBlock, {raw, one}, IntBitwiseKind::BITWISE_OR);
            ValueVertex *normalized = self->NewVertex<RawI64ToTaggedVertex>(
                compileInfoFacts_, currentBlock, {normalizedRaw});
            ValueVertex *nullRoot = self->graph_->GetTaggedConstant(
                static_cast<uint64_t>(JSTaggedValue::VALUE_NULL));
            ValueVertex *isEqual = BuildTaggedEqual(normalized, nullRoot);
            if (IsPositiveEqualityCompare(kind)) {
                return isEqual;
            }
            // Boolean negation via the TaggedNotEqual(x, true) idiom; branch
            // consumers re-fuse the polarity, so the chain stays cheap.
            return BuildTaggedNotEqual(isEqual, GetBooleanConstant(true));
        }
        return nullptr;
    }

    ValueVertex *TryReduceCompareEqualAgainstConstant(JSCondition kind, ValueVertex *left, ValueVertex *right)
    {
        if (left == right && IsEqualityCompare(kind) && compileInfoFacts_->CheckType(left, NodeInfo::NodeType::INT)) {
            return GetBooleanConstant(IsPositiveEqualityCompare(kind));
        }
        if (!IsStrictEqualityCompare(kind)) {
            // Relational comparisons (<, <=, >, >=, those that are neither strict equality nor loose equality)
            // are skipped currently. Their optimization is TBD later.
            return IsLooseEqualityCompare(kind) ? TryReduceLooseEqualityAgainstConstant(kind, left, right) : nullptr;
        }
        bool equalResult = IsPositiveEqualityCompare(kind);
        NodeInfo::NodeType leftType = compileInfoFacts_->GetKnownType(left);
        NodeInfo::NodeType rightType = compileInfoFacts_->GetKnownType(right);
        if (left == right && !NodeInfo::NodeTypeCanBe(leftType, NodeInfo::NodeType::NUMBER)) {
            return GetBooleanConstant(equalResult);
        }
        if (!StrictTypesCanBeEqual(leftType, rightType)) {
            return GetBooleanConstant(!equalResult);
        }
        if (IsReferenceComparableRootValue(left) || IsReferenceComparableRootValue(right)) {
            return equalResult ? BuildTaggedEqual(left, right) : BuildTaggedNotEqual(left, right);
        }
        if (IsReferenceComparableType(leftType) && IsReferenceComparableType(rightType)) {
            return equalResult ? BuildTaggedEqual(left, right) : BuildTaggedNotEqual(left, right);
        }
        return nullptr;
    }

    ValueVertex *BuildI32CompareTaggedValue(JSCondition kind, ValueVertex *left, ValueVertex *right)
    {
        std::optional<int32_t> leftValue = TryGetInt32Value(left);
        std::optional<int32_t> rightValue = TryGetInt32Value(right);
        if (leftValue.has_value() && rightValue.has_value()) {
            return GetBooleanConstant(EvaluateCompare(kind, *leftValue, *rightValue));
        }

        ValueVertex *leftI32 = BuildTaggedIntToI32(left);
        ValueVertex *rightI32 = BuildTaggedIntToI32(right);
        ValueVertex *result = self->NewVertex<I32ConditionCheckVertex>(
            compileInfoFacts_, currentBlock, {leftI32, rightI32}, ToMachineCondition(kind));
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
        return result;
    }

    ValueVertex *BuildI32CompareOp(JSCondition kind, ValueVertex *left, ValueVertex *right, bool leftKnownInt,
                                   bool rightKnownInt)
    {
        if (leftKnownInt && rightKnownInt) {
            return BuildI32CompareTaggedValue(kind, left, right);
        }

        ValueVertex *leftI32 = leftKnownInt ? BuildTaggedIntToI32(left) : BuildCheckedTaggedIntToI32(left);
        ValueVertex *rightI32 = rightKnownInt ? BuildTaggedIntToI32(right) : BuildCheckedTaggedIntToI32(right);
        ValueVertex *result = self->NewVertex<I32ConditionCheckVertex>(
            compileInfoFacts_, currentBlock, {leftI32, rightI32}, ToMachineCondition(kind));
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
        return result;
    }

    ValueVertex *BuildF64CompareTaggedValue(JSCondition kind, ValueVertex *leftF64, ValueVertex *rightF64)
    {
        if (auto *leftConst = leftF64->TryCast<Float64ConstantVertex>()) {
            if (auto *rightConst = rightF64->TryCast<Float64ConstantVertex>()) {
                return GetBooleanConstant(EvaluateCompare(kind, leftConst->GetValue(), rightConst->GetValue()));
            }
        }

        ValueVertex *result = self->NewVertex<F64ConditionCheckVertex>(
            compileInfoFacts_, currentBlock, {leftF64, rightF64}, ToMachineCondition(kind));
        compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
        return result;
    }

    ValueVertex *BuildF64CompareOp(JSCondition kind, ValueVertex *left, ValueVertex *right)
    {
        ValueVertex *leftF64 = BuildCheckedNumberToF64(left);
        ValueVertex *rightF64 = BuildCheckedNumberToF64(right);
        return BuildF64CompareTaggedValue(kind, leftF64, rightF64);
    }

    ValueVertex *BuildStringCompareOp(JSCondition kind, ValueVertex *left, ValueVertex *right)
    {
        if (kind == JSCondition::EQUAL || kind == JSCondition::STRICT_EQUAL) {
            ValueVertex *result = self->NewVertex<StringEqualVertex>(
                compileInfoFacts_, currentBlock, {glue, left, right, GlobalEnv()});
            compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::BOOLEAN);
            return result;
        }
        if (kind == JSCondition::NOT_EQUAL || kind == JSCondition::STRICT_NOT_EQUAL) {
            // NOT_EQUAL = !EQUAL: build StringEqual then negate the boolean.
            ValueVertex *equal = BuildStringCompareOp(NegateJSCondition(kind), left, right);
            return BuildTaggedNotEqual(equal, GetBooleanConstant(true));
        }
        return BuildGenericCompareOp(kind, left, right);
    }

    ValueVertex *TryBuildStringCompareOp(JSCondition kind, ValueVertex *left, ValueVertex *right,
                                         const OperationFeedback &feedback)
    {
        bool leftKnownString = compileInfoFacts_->CheckType(left, NodeInfo::NodeType::STRING);
        bool rightKnownString = compileInfoFacts_->CheckType(right, NodeInfo::NodeType::STRING);
        if (leftKnownString && rightKnownString) {
            return BuildStringCompareOp(kind, left, right);
        }
        if (feedback.hint != ArkSteedOperationHint::STRING) {
            return nullptr;
        }
        LogOperationFeedback(feedback, "CompareString", "StringCompareOp");
        ValueVertex *checkedLeft = leftKnownString ? left : BuildCheckedTaggedString(left);
        ValueVertex *checkedRight = rightKnownString ? right : BuildCheckedTaggedString(right);
        return BuildStringCompareOp(kind, checkedLeft, checkedRight);
    }

    ValueVertex *TryFoldUint32ComparedToNonPositive(JSCondition kind, ValueVertex *left, ValueVertex *right)
    {
        if (left == nullptr || !left->Is<CheckedNonNegativeI32ToTaggedIntVertex>()) {
            return nullptr;
        }
        std::optional<int32_t> rightValue = TryGetInt32Value(right);
        if (!rightValue.has_value() || *rightValue > 0) {
            return nullptr;
        }
        switch (kind) {
            case JSCondition::GREATER_THAN_OR_EQUAL:
                return GetBooleanConstant(true);  // uint32 >= 0 >= right
            case JSCondition::LESS_THAN:
                return GetBooleanConstant(false);  // uint32 >= 0, cannot be < right (<=0)
            case JSCondition::GREATER_THAN:
                if (*rightValue < 0) {
                    return GetBooleanConstant(true);  // uint32 >= 0 > right
                }
                return nullptr;  // right == 0: uint32 > 0 not always (could be 0)
            default:
                return nullptr;  // LE / equality: not always-resolvable
        }
    }

    ValueVertex *BuildCompareOperation(JSCondition kind)
    {
        ValueVertex *left = LoadRegister(currentBcInfo, 0);
        ValueVertex *right = frameState.GetAcc();

        if (ValueVertex *result = TryReduceCompareEqualAgainstConstant(kind, left, right)) {
            return result;
        }

        if (ValueVertex *result = TryFoldUint32ComparedToNonPositive(kind, left, right)) {
            return result;
        }

        bool leftKnownInt = compileInfoFacts_->CheckType(left, NodeInfo::NodeType::INT);
        bool rightKnownInt = compileInfoFacts_->CheckType(right, NodeInfo::NodeType::INT);
        if (leftKnownInt && rightKnownInt) {
            return BuildI32CompareOp(kind, left, right, leftKnownInt, rightKnownInt);
        }

        OperationFeedback feedback = self->pgoContext_.ReadOperationFeedback(currentBcInfo);
        if (feedback.hint == ArkSteedOperationHint::INT) {
            LogOperationFeedback(feedback, "Compare", "I32CompareOp");
            return BuildI32CompareOp(kind, left, right, false, false);
        }

        bool leftKnownNumber = compileInfoFacts_->CheckType(left, NodeInfo::NodeType::NUMBER);
        bool rightKnownNumber = compileInfoFacts_->CheckType(right, NodeInfo::NodeType::NUMBER);
        bool leftKnownNonIntNumber = leftKnownNumber && !leftKnownInt;
        bool rightKnownNonIntNumber = rightKnownNumber && !rightKnownInt;
        if (leftKnownNonIntNumber || rightKnownNonIntNumber || feedback.hint == ArkSteedOperationHint::NUMBER) {
            LogOperationFeedback(feedback, "Compare", "F64CompareOp");
            return BuildF64CompareOp(kind, left, right);
        }

        if (leftKnownInt && rightKnownInt) {
            return BuildI32CompareOp(kind, left, right, leftKnownInt, rightKnownInt);
        }

        if (ValueVertex *stringCompare = TryBuildStringCompareOp(kind, left, right, feedback)) {
            return stringCompare;
        }

        if (IsEqualityCompare(kind) && left == right &&
            compileInfoFacts_->CheckType(left, NodeInfo::NodeType::STRING)) {
            return GetBooleanConstant(IsPositiveEqualityCompare(kind));
        }

        return BuildGenericCompareOp(kind, left, right);
    }

    ValueVertex *BuildGenericBinOp(BinaryOpKind kind, ValueVertex *left, ValueVertex *right)
    {
        switch (kind) {
            case BinaryOpKind::ADD:
                return CommonStubCall({glue, left, right, GlobalEnv()}, CommonStubID::Add);
            case BinaryOpKind::SUB:
                return CommonStubCall({glue, left, right, GlobalEnv()}, CommonStubID::Sub);
            case BinaryOpKind::MUL:
                return CommonStubCall({glue, left, right, GlobalEnv()}, CommonStubID::Mul);
            case BinaryOpKind::DIV:
                return CommonStubCall({glue, left, right, GlobalEnv()}, CommonStubID::Div);
            case BinaryOpKind::MOD:
                return CommonStubCall({glue, left, right, GlobalEnv()}, CommonStubID::Mod);
            case BinaryOpKind::EXP:
                return RuntimeCall({left, right}, RTSTUB_ID(Exp));
            default:
                break;
        }
        UNREACHABLE();
    }

    void LogOperationFeedback(const OperationFeedback &feedback, const char *opcodeName, const char *chosenPath) const
    {
        LOG_COMPILER(INFO) << "ArkSteed operation feedback: op=" << opcodeName
                           << ", bcOffset=" << self->preproc_->GetBytecodeOffset(currentBcIndex)
                           << ", slotId=" << feedback.slotId << ", rawTypeBits=" << feedback.rawTypeBits
                           << ", hint=" << static_cast<uint32_t>(feedback.hint) << ", path=" << chosenPath;
    }

    ValueVertex *BuildNumericBinOp(BinaryOpKind kind, ValueVertex *left, ValueVertex *right,
                                   const OperationFeedback &feedback)
    {
        if (feedback.hint == ArkSteedOperationHint::INT && SupportsI32CheckedBinOp(kind)) {
            LogOperationFeedback(feedback, "BinaryNumeric", "I32BinOp");
            return BuildI32BinOp(kind, left, right);
        }
        if (feedback.hint == ArkSteedOperationHint::NUMBER && SupportsF64BinOp(kind)) {
            LogOperationFeedback(feedback, "BinaryNumeric", "F64NumberBinOp");
            return BuildF64NumberBinOp(kind, left, right);
        }
        LogOperationFeedback(feedback, "BinaryNumeric", "GenericBinOp");
        return BuildGenericBinOp(kind, left, right);
    }

    ValueVertex *BuildBinaryOperation(BinaryOpKind kind)
    {
        ValueVertex *left = LoadRegister(currentBcInfo, 0);
        ValueVertex *right = frameState.GetAcc();
        if (ValueVertex *constant = TryBuildConstantBinaryOperation(kind, left, right)) {
            JSTaggedValue value(constant->Cast<TaggedConstantVertex>()->GetValue());
            compileInfoFacts_->EnsureType(constant, NodeTypeFromJSTaggedValue(value));
            return constant;
        }
        OperationFeedback feedback = self->pgoContext_.ReadOperationFeedback(currentBcInfo);
        bool observedNonInt32Result = SupportsF64BinOp(kind) && feedback.hint == ArkSteedOperationHint::NUMBER;
        if (!observedNonInt32Result) {
            if (ValueVertex *provenInt = TryBuildProvenIntBinOp(kind, left, right)) {
                return provenInt;
            }
        }
        if (kind == BinaryOpKind::ADD) {
            if (ValueVertex *stringAdd = TryBuildStringAdd(left, right, feedback)) {
                return stringAdd;
            }
        }
        return BuildNumericBinOp(kind, left, right, feedback);
    }

    ValueVertex *BuildGenericUnaryOp(CommonStubID stubId, ValueVertex *value)
    {
        return CommonStubCall({glue, value}, stubId);
    }

    ValueVertex *BuildIntUnaryOp(CommonStubID stubId, ValueVertex *value, bool valueKnownInt)
    {
        ValueVertex *valueI32 = valueKnownInt ? BuildTaggedIntToI32(value) : BuildCheckedTaggedIntToI32(value);
        std::vector<ValueVertex *> inputs {valueI32};

        switch (stubId) {
            case CommonStubID::Inc: {
                ValueVertex *rawResult = BuildCheckedI32Arithmetic<I32IncWithOverflowVertex>({valueI32});
                return BuildTaggedI32Result(rawResult);
            }
            case CommonStubID::Dec: {
                ValueVertex *rawResult = BuildCheckedI32Arithmetic<I32DecWithOverflowVertex>({valueI32});
                return BuildTaggedI32Result(rawResult);
            }
            case CommonStubID::Neg: {
                EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
                ValueVertex *rawResult = self->NewVertex<I32NegWithOverflowVertex>(
                    currentBlock, inputs, self->chunk_, self->preproc_->GetBytecodeOffset(currentBcIndex));
                rawResult->Cast<I32NegWithOverflowVertex>()->SetEagerDeoptFrameState(std::move(deoptFrameState));
                return BuildTaggedI32Result(rawResult);
            }
            case CommonStubID::Not: {
                ValueVertex *rawResult = self->NewVertex<I32BNotVertex>(
                    compileInfoFacts_, currentBlock, std::initializer_list<ValueVertex *> {valueI32});
                return BuildTaggedI32Result(rawResult);
            }
            default:
                return BuildGenericUnaryOp(stubId, value);
        }
    }

    ValueVertex *BuildTruncatingI32BNot(ValueVertex *value)
    {
        if (compileInfoFacts_->CheckType(value, NodeInfo::NodeType::INT)) {
            ValueVertex *valueI32 = BuildTaggedIntToI32(value);
            ValueVertex *rawResult = self->NewVertex<I32BNotVertex>(compileInfoFacts_, currentBlock, {valueI32});
            return BuildTaggedI32Result(rawResult);
        }
        ValueVertex *valueF64 = BuildCheckedNumberToF64(value);
        ValueVertex *truncI32 = self->NewVertex<F64ToI32TruncVertex>(compileInfoFacts_, currentBlock, {valueF64});
        ValueVertex *rawResult = self->NewVertex<I32BNotVertex>(compileInfoFacts_, currentBlock, {truncI32});
        return BuildTaggedI32Result(rawResult);
    }

    ValueVertex *BuildF64UnaryOp(CommonStubID stubId, ValueVertex *value)
    {
        ValueVertex *valueF64 = BuildCheckedNumberToF64(value);
        switch (stubId) {
            case CommonStubID::Neg: {
                ValueVertex *negF64 = self->NewVertex<F64NegVertex>(compileInfoFacts_, currentBlock, {valueF64});
                ValueVertex *result =
                    self->NewVertex<F64ToTaggedDoubleVertex>(compileInfoFacts_, currentBlock, {negF64});
                compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::NUMBER);
                return result;
            }
            case CommonStubID::Inc: {
                ValueVertex *oneF64 = self->graph_->GetFloat64Constant(1.0);
                ValueVertex *addF64 =
                    self->NewVertex<F64AddVertex>(compileInfoFacts_, currentBlock, {valueF64, oneF64});
                ValueVertex *result =
                    self->NewVertex<F64ToTaggedDoubleVertex>(compileInfoFacts_, currentBlock, {addF64});
                compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::NUMBER);
                return result;
            }
            case CommonStubID::Dec: {
                ValueVertex *oneF64 = self->graph_->GetFloat64Constant(1.0);
                ValueVertex *subF64 =
                    self->NewVertex<F64SubVertex>(compileInfoFacts_, currentBlock, {valueF64, oneF64});
                ValueVertex *result =
                    self->NewVertex<F64ToTaggedDoubleVertex>(compileInfoFacts_, currentBlock, {subF64});
                compileInfoFacts_->EnsureType(result, NodeInfo::NodeType::NUMBER);
                return result;
            }
            case CommonStubID::Not:
                return BuildTruncatingI32BNot(value);
            default:
                return BuildGenericUnaryOp(stubId, value);
        }
    }

    ValueVertex *BuildUnaryOperation(CommonStubID stubId)
    {
        ValueVertex *value = frameState.GetAcc();
        if (ValueVertex *constant = TryBuildConstantUnaryOperation(stubId, value)) {
            return constant;
        }

        OperationFeedback feedback = self->pgoContext_.ReadOperationFeedback(currentBcInfo);
        bool valueKnownInt = compileInfoFacts_->CheckType(value, NodeInfo::NodeType::INT);
        bool hasIntFeedback = feedback.hint == ArkSteedOperationHint::INT;
        bool hasNumberFeedback = feedback.hint == ArkSteedOperationHint::NUMBER ||
                                 feedback.hint == ArkSteedOperationHint::NUMBER_OR_STRING;

        switch (stubId) {
            case CommonStubID::Neg:
            case CommonStubID::Inc:
            case CommonStubID::Dec:
                if (hasNumberFeedback) {
                    // Int32 inputs can still produce -0 or overflow results. Checking valueKnownInt first would
                    // select Int32 lowering for these sites and repeatedly deoptimize.
                    LogOperationFeedback(feedback, "Unary", "F64UnaryOp");
                    return BuildF64UnaryOp(stubId, value);
                }
                if (valueKnownInt) {
                    return BuildIntUnaryOp(stubId, value, true);
                }
                if (hasIntFeedback) {
                    LogOperationFeedback(feedback, "Unary", "IntUnaryOp");
                    return BuildIntUnaryOp(stubId, value, false);
                }
                break;
            case CommonStubID::Not:
                if (valueKnownInt) {
                    return BuildIntUnaryOp(stubId, value, true);
                }
                if (hasIntFeedback) {
                    LogOperationFeedback(feedback, "Unary", "IntUnaryOp");
                    return BuildIntUnaryOp(stubId, value, false);
                }
                if (hasNumberFeedback) {
                    LogOperationFeedback(feedback, "Unary", "F64UnaryOp");
                    return BuildF64UnaryOp(stubId, value);
                }
                break;
            default:
                UNREACHABLE();
        }

        LogOperationFeedback(feedback, "Unary", "GenericUnaryOp");
        return BuildGenericUnaryOp(stubId, value);
    }

    ValueVertex *TryBuildConstantUnaryOperation(CommonStubID stubId, ValueVertex *value)
    {
        if (std::optional<int32_t> intValue = TryGetInt32Value(value)) {
            return TryBuildInt32ConstantUnaryOperation(stubId, *intValue);
        }
        auto *float64Constant = value->TryCast<Float64ConstantVertex>();
        if (float64Constant == nullptr) {
            return nullptr;
        }
        return TryBuildFloat64ConstantUnaryOperation(stubId, float64Constant->GetValue());
    }

    ValueVertex *TryBuildInt32ConstantUnaryOperation(CommonStubID stubId, int32_t value)
    {
        switch (stubId) {
            case CommonStubID::Inc:
                if (value == std::numeric_limits<int32_t>::max()) {
                    return nullptr;
                }
                return TaggedConstantFromFoldedValue(JSTaggedValue(value + 1));
            case CommonStubID::Dec:
                if (value == std::numeric_limits<int32_t>::min()) {
                    return nullptr;
                }
                return TaggedConstantFromFoldedValue(JSTaggedValue(value - 1));
            case CommonStubID::Neg:
                if (value == 0 || value == std::numeric_limits<int32_t>::min()) {
                    return nullptr;
                }
                return TaggedConstantFromFoldedValue(JSTaggedValue(-value));
            case CommonStubID::Not:
                return TaggedConstantFromFoldedValue(JSTaggedValue(~value));
            default:
                return nullptr;
        }
    }

    ValueVertex *TryBuildFloat64ConstantUnaryOperation(CommonStubID stubId, double value)
    {
        switch (stubId) {
            case CommonStubID::Inc:
                return TaggedConstantFromFoldedValue(JSTaggedValue(value + 1.0));
            case CommonStubID::Dec:
                return TaggedConstantFromFoldedValue(JSTaggedValue(value - 1.0));
            case CommonStubID::Neg:
                return TaggedConstantFromFoldedValue(JSTaggedValue(-value));
            default:
                return nullptr;
        }
    }

    ValueVertex *TryBuildFloat64ConstantBinaryOperation(BinaryOpKind kind, double lhs, double rhs)
    {
        double result = 0.0;
        switch (kind) {
            case BinaryOpKind::ADD:
                result = lhs + rhs;
                break;
            case BinaryOpKind::SUB:
                result = lhs - rhs;
                break;
            case BinaryOpKind::MUL:
                result = lhs * rhs;
                break;
            case BinaryOpKind::DIV:
                if (rhs == 0.0) {
                    // x / 0 -> Infinity / -Infinity / NaN; the sign and NaN rules are best left
                    // to the runtime F64DivVertex rather than reproduced at compile time.
                    return nullptr;
                }
                result = lhs / rhs;
                break;
            default:
                return nullptr;
        }
        return TaggedConstantFromFoldedValue(JSTaggedValue(result));
    }

    ValueVertex *TryBuildConstantBinaryOperation(BinaryOpKind kind, ValueVertex *left, ValueVertex *right)
    {
        std::optional<int32_t> leftValue = TryGetInt32Value(left);
        std::optional<int32_t> rightValue = TryGetInt32Value(right);
        if (leftValue.has_value() && rightValue.has_value()) {
            int32_t lhs = *leftValue;
            int32_t rhs = *rightValue;
            int64_t result = 0;
            switch (kind) {
                case BinaryOpKind::ADD:
                    result = static_cast<int64_t>(lhs) + static_cast<int64_t>(rhs);
                    break;
                case BinaryOpKind::SUB:
                    result = static_cast<int64_t>(lhs) - static_cast<int64_t>(rhs);
                    break;
                case BinaryOpKind::MUL:
                    if ((lhs == 0 || rhs == 0) && (lhs < 0 || rhs < 0)) {
                        return nullptr;
                    }
                    result = static_cast<int64_t>(lhs) * static_cast<int64_t>(rhs);
                    break;
                default:
                    // Int DIV/MOD/EXP are left unfolded.
                    return nullptr;
            }
            if (result < std::numeric_limits<int32_t>::min() || result > std::numeric_limits<int32_t>::max()) {
                return nullptr;
            }
            return self->graph_->GetTaggedConstant(JSTaggedValue(static_cast<int32_t>(result)).GetRawData());
        }

        // Float64 two-constant fold (ADD/SUB/MUL/DIV). Only fires for double constants, so int
        // DIV/MOD are unaffected.
        std::optional<double> leftF64 = TryGetFloat64Value(left);
        std::optional<double> rightF64 = TryGetFloat64Value(right);
        if (leftF64.has_value() && rightF64.has_value()) {
            return TryBuildFloat64ConstantBinaryOperation(kind, *leftF64, *rightF64);
        }
        return nullptr;
    }

    enum class HClassCheckResult : uint8_t {
        SUCCESS,
        UNREACHABLE,
        FAILURE,
    };

    struct RequestedHClassInfo {
        JSHClass *hclass {nullptr};
        ArkSteedHClassRef hclassRef {};
        bool hasExternalStableDependency {false};
    };

    class KnownHClassesMerger {
    public:
        KnownHClassesMerger(CompileInfoFacts *facts, ValueVertex *object,
                            const std::vector<RequestedHClassInfo> &requestedHClasses)
            : knownType_(facts->GetKnownType(object))
        {
            if (requestedHClasses.empty()) {
                return;
            }
            inputIsValid_ = true;
            for (const RequestedHClassInfo &requested : requestedHClasses) {
                if (requested.hclass == nullptr) {
                    inputIsValid_ = false;
                    requestedHClasses_.clear();
                    requestedHClassPointers_.clear();
                    return;
                }
                NodeInfo::NodeType hclassType = NodeTypeFromHClass(requested.hclass);
                if (!NodeInfo::NodeTypeCanBe(knownType_, hclassType)) {
                    continue;
                }
                auto existing =
                    std::find_if(requestedHClasses_.begin(), requestedHClasses_.end(),
                                 [&requested](const auto &entry) { return entry.hclass == requested.hclass; });
                if (existing != requestedHClasses_.end()) {
                    existing->hasExternalStableDependency |= requested.hasExternalStableDependency;
                    if (!existing->hclassRef.IsSafeForCompile() && requested.hclassRef.IsSafeForCompile()) {
                        existing->hclassRef = requested.hclassRef;
                    }
                    continue;
                }
                requestedHClasses_.push_back(requested);
                requestedHClassPointers_.push_back(requested.hclass);
            }

            std::optional<NodeInfo::PossibleHClasses> knownHClasses = facts->TryGetPossibleHClasses(object);
            if (!knownHClasses.has_value()) {
                return;
            }
            existingFreshHClassesFound_ = true;
            knownHClasses_ = std::move(knownHClasses.value());
            knownHClassesAreSubset_ = true;
            for (JSHClass *knownHClass : knownHClasses_) {
                if (FindRequestedInfo(knownHClass) == nullptr) {
                    knownHClassesAreSubset_ = false;
                    continue;
                }
                intersectSet_.push_back(knownHClass);
            }
        }

        bool InputIsValid() const
        {
            return inputIsValid_;
        }

        bool RequestedSetIsEmpty() const
        {
            return requestedHClasses_.empty();
        }

        bool ExistingFreshHClassesFound() const
        {
            return existingFreshHClassesFound_;
        }

        bool KnownHClassesAreSubset() const
        {
            return existingFreshHClassesFound_ && knownHClassesAreSubset_;
        }

        const NodeInfo::PossibleHClasses &KnownHClasses() const
        {
            return knownHClasses_;
        }

        const NodeInfo::PossibleHClasses &RequestedHClasses() const
        {
            return requestedHClassPointers_;
        }

        const NodeInfo::PossibleHClasses &IntersectSet() const
        {
            return intersectSet_;
        }

        const RequestedHClassInfo *FindRequestedInfo(JSHClass *hclass) const
        {
            auto it = std::find_if(requestedHClasses_.begin(), requestedHClasses_.end(),
                                   [hclass](const auto &entry) { return entry.hclass == hclass; });
            return it == requestedHClasses_.end() ? nullptr : &*it;
        }

    private:
        NodeInfo::NodeType knownType_ {NodeInfo::NodeType::UNKNOWN};
        std::vector<RequestedHClassInfo> requestedHClasses_;
        NodeInfo::PossibleHClasses requestedHClassPointers_;
        NodeInfo::PossibleHClasses knownHClasses_;
        NodeInfo::PossibleHClasses intersectSet_;
        bool inputIsValid_ {false};
        bool existingFreshHClassesFound_ {false};
        bool knownHClassesAreSubset_ {false};
    };

    HClassCheckResult EmitUnconditionalHClassDeopt(uint32_t bcIndex)
    {
        auto *deopt =
            self->FinishBlockWith<DeoptVertex>(currentBlock, {}, self->chunk_, kungfu::DeoptType::KEYMISSMATCH,
                                               self->preproc_->GetBytecodeOffset(bcIndex));
        deopt->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(bcIndex));
        return HClassCheckResult::UNREACHABLE;
    }

    HClassCheckResult EmitHClassCheck(uint32_t bcIndex, ValueVertex *object, const NodeInfo::PossibleHClasses &hclasses,
                                      const KnownHClassesMerger &merger)
    {
        ASSERT(!hclasses.empty());
        std::vector<uint32_t> expectedHClassHandleIndices;
        expectedHClassHandleIndices.reserve(hclasses.size());
        for (JSHClass *hclass : hclasses) {
            const RequestedHClassInfo *requested = merger.FindRequestedInfo(hclass);
            if (requested == nullptr || !requested->hclassRef.IsSafeForCompile()) {
                return HClassCheckResult::FAILURE;
            }
            std::optional<uint32_t> handleIndex = GetHeapConstantHandleIndex(requested->hclassRef);
            if (!handleIndex.has_value()) {
                return HClassCheckResult::FAILURE;
            }
            expectedHClassHandleIndices.push_back(handleIndex.value());
        }
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(bcIndex);
        if (hclasses.size() == 1) {
            auto *check = self->NewVertex<DeoptIfHClassMismatchVertex>(currentBlock, {object}, self->chunk_,
                                                                       expectedHClassHandleIndices.front(),
                                                                       self->preproc_->GetBytecodeOffset(bcIndex));
            check->SetEagerDeoptFrameState(std::move(deoptFrameState));
            return HClassCheckResult::SUCCESS;
        }
        auto *check =
            self->NewVertex<DeoptIfHClassNotInVertex>(currentBlock, {object}, self->chunk_, expectedHClassHandleIndices,
                                                      self->preproc_->GetBytecodeOffset(bcIndex));
        check->SetEagerDeoptFrameState(std::move(deoptFrameState));
        return HClassCheckResult::SUCCESS;
    }

    bool IsHClassStableForFacts(JSHClass *hclass, bool hasExternalStableDependency) const
    {
        return JSTaggedValue(hclass).IsInSharedHeap() || (self->IsLazyDeoptEnabled() && hasExternalStableDependency);
    }

    HClassCheckResult ResolveHClassStability(const NodeInfo::PossibleHClasses &hclasses,
                                             const KnownHClassesMerger &merger, bool installStableDependencies,
                                             NodeInfo::PossibleHClassInfos *resolvedHClasses)
    {
        ASSERT(resolvedHClasses != nullptr);
        resolvedHClasses->clear();
        resolvedHClasses->reserve(hclasses.size());
        for (JSHClass *hclass : hclasses) {
            const RequestedHClassInfo *requested = merger.FindRequestedInfo(hclass);
            ASSERT(requested != nullptr);
            bool isSharedHClass = JSTaggedValue(hclass).IsInSharedHeap();
            bool isStable = IsHClassStableForFacts(hclass, requested->hasExternalStableDependency);
            bool shouldInstallDependency =
                installStableDependencies && self->IsLazyDeoptEnabled() && !isSharedHClass && !isStable;
            if (shouldInstallDependency &&
                !self->pgoContext_.GetBroker()->TryInstallStableHClassDependency(hclass, &isStable)) {
                return HClassCheckResult::FAILURE;
            }
            resolvedHClasses->push_back(NodeInfo::PossibleHClassInfo {
                .hclass = hclass,
                .isStable = isStable,
            });
        }
        return HClassCheckResult::SUCCESS;
    }

    std::optional<HClassCheckResult> TryFoldConstantHClassCheck(uint32_t bcIndex, ValueVertex *object,
                                                                const KnownHClassesMerger &merger,
                                                                bool installStableDependencies)
    {
        std::optional<ArkSteedHeapRef> constantRef = TryGetConstantHeapRef(object);
        if (!constantRef.has_value()) {
            return std::nullopt;
        }
        ArkSteedHeapBroker *broker = self->pgoContext_.GetBroker();
        if (broker == nullptr) {
            return HClassCheckResult::FAILURE;
        }
        if (!constantRef->IsHeapObject()) {
            return EmitUnconditionalHClassDeopt(bcIndex);
        }
        JSHClass *constantHClass = broker->TryGetObjectHClass(*constantRef);
        if (constantHClass == nullptr) {
            return HClassCheckResult::FAILURE;
        }
        if (merger.FindRequestedInfo(constantHClass) == nullptr) {
            return EmitUnconditionalHClassDeopt(bcIndex);
        }
        NodeInfo::PossibleHClassInfos resolvedHClasses;
        HClassCheckResult result =
            ResolveHClassStability({constantHClass}, merger, installStableDependencies, &resolvedHClasses);
        if (result != HClassCheckResult::SUCCESS) {
            return result;
        }
        if (resolvedHClasses.front().isStable) {
            compileInfoFacts_->RecordHClass(object, constantHClass, true);
            return HClassCheckResult::SUCCESS;
        }
        return std::nullopt;
    }

    HClassCheckResult BuildHClassCheck(uint32_t bcIndex, ValueVertex *object,
                                       const std::vector<RequestedHClassInfo> &requestedHClasses,
                                       bool installStableDependencies)
    {
        KnownHClassesMerger merger(compileInfoFacts_, object, requestedHClasses);
        if (!merger.InputIsValid()) {
            return HClassCheckResult::FAILURE;
        }
        if (merger.RequestedSetIsEmpty()) {
            return EmitUnconditionalHClassDeopt(bcIndex);
        }
        if (std::optional<HClassCheckResult> constantResult =
                TryFoldConstantHClassCheck(bcIndex, object, merger, installStableDependencies)) {
            return constantResult.value();
        }
        if (merger.KnownHClassesAreSubset()) {
            return HClassCheckResult::SUCCESS;
        }

        const NodeInfo::PossibleHClasses &checkedHClasses =
            merger.ExistingFreshHClassesFound() ? merger.IntersectSet() : merger.RequestedHClasses();
        if (checkedHClasses.empty()) {
            return EmitUnconditionalHClassDeopt(bcIndex);
        }
        NodeInfo::PossibleHClassInfos resolvedHClasses;
        HClassCheckResult stabilityResult =
            ResolveHClassStability(checkedHClasses, merger, installStableDependencies, &resolvedHClasses);
        if (stabilityResult != HClassCheckResult::SUCCESS) {
            return stabilityResult;
        }

        HClassCheckResult emissionResult = EmitHClassCheck(bcIndex, object, checkedHClasses, merger);
        if (emissionResult != HClassCheckResult::SUCCESS) {
            return emissionResult;
        }
        if (merger.ExistingFreshHClassesFound()) {
            bool narrowed = compileInfoFacts_->NarrowPossibleHClasses(object, checkedHClasses);
            ASSERT(narrowed);
            for (const NodeInfo::PossibleHClassInfo &info : resolvedHClasses) {
                if (info.isStable) {
                    bool marked = compileInfoFacts_->MarkPossibleHClassStable(object, info.hclass);
                    ASSERT(marked);
                }
            }
        } else {
            compileInfoFacts_->RecordPossibleHClasses(object, resolvedHClasses);
        }
        return HClassCheckResult::SUCCESS;
    }

    HClassCheckResult BuildCheckSingleHClass(uint32_t bcIndex, ValueVertex *object, JSHClass *hclass,
                                             const ArkSteedHClassRef &hclassRef, bool installStableDependency,
                                             bool hasExternalStableDependency)
    {
        std::vector<RequestedHClassInfo> requested {{hclass, hclassRef, hasExternalStableDependency}};
        return BuildHClassCheck(bcIndex, object, requested, installStableDependency);
    }

    HClassCheckResult BuildCheckAnyOfHClasses(uint32_t bcIndex, ValueVertex *object,
                                              const std::vector<JSHClass *> &hclasses,
                                              const std::vector<ArkSteedHClassRef> &hclassRefs,
                                              bool installStableDependencies, bool hasExternalStableDependencies)
    {
        if (hclasses.size() != hclassRefs.size()) {
            return HClassCheckResult::FAILURE;
        }
        std::vector<RequestedHClassInfo> requested;
        requested.reserve(hclasses.size());
        for (size_t i = 0; i < hclasses.size(); ++i) {
            requested.push_back(RequestedHClassInfo {hclasses[i], hclassRefs[i], hasExternalStableDependencies});
        }
        return BuildHClassCheck(bcIndex, object, requested, installStableDependencies);
    }

    class HClassInference {
    public:
        enum class Mode : uint8_t {
            FRESH_ONLY,
            ALLOW_STALE,
        };

        HClassInference(BytecodeVisitor *builder, ValueVertex *object, Mode mode = Mode::FRESH_ONLY)
            : builder_(builder),
              object_(object),
              facts_(builder->compileInfoFacts_),
              effectEpoch_(facts_->GetEffectEpoch())
        {
            std::optional<NodeInfo::PossibleHClasses> hclasses =
                mode == Mode::FRESH_ONLY ? facts_->TryGetPossibleHClasses(object_)
                                         : facts_->TryGetPossibleHClassesForRevalidation(object_);
            if (hclasses.has_value()) {
                hclasses_ = std::move(hclasses.value());
                allHClassesAreFresh_ = !facts_->PossibleHClassesAreStale(object_);
                return;
            }
            if (mode == Mode::FRESH_ONLY) {
                return;
            }
            std::optional<ArkSteedHeapRef> constantRef = builder_->TryGetConstantHeapRef(object_);
            ArkSteedHeapBroker *broker = builder_->self->pgoContext_.GetBroker();
            if (!constantRef.has_value() || broker == nullptr) {
                return;
            }
            if (auto *hclass = broker->TryGetObjectHClass(*constantRef)) {
                hclasses_.push_back(hclass);
                hclassesComeFromConstant_ = true;
            }
        }

        bool HasHClasses() const
        {
            return !hclasses_.empty();
        }

        bool AllHClassesAreFresh() const
        {
            return HasHClasses() && allHClassesAreFresh_;
        }

        std::optional<NodeInfo::PossibleHClasses> TryGetPossibleHClasses() const
        {
            return HasHClasses() ? std::optional<NodeInfo::PossibleHClasses>(hclasses_) : std::nullopt;
        }

        HClassCheckResult EnsureHClassesFresh(uint32_t bcIndex)
        {
            if (!HasHClasses()) {
                return HClassCheckResult::FAILURE;
            }
            ASSERT(facts_ == builder_->compileInfoFacts_);
            ASSERT(effectEpoch_ == facts_->GetEffectEpoch());
            std::optional<NodeInfo::PossibleHClasses> freshHClasses = facts_->TryGetPossibleHClasses(object_);
            if (freshHClasses.has_value()) {
                if (!ContainsSameHClasses(freshHClasses.value(), hclasses_)) {
                    return HClassCheckResult::FAILURE;
                }
                allHClassesAreFresh_ = true;
                return HClassCheckResult::SUCCESS;
            }
            if (allHClassesAreFresh_) {
                return HClassCheckResult::FAILURE;
            }

            std::optional<NodeInfo::PossibleHClasses> storedHClasses;
            if (!hclassesComeFromConstant_) {
                storedHClasses = facts_->TryGetPossibleHClassesForRevalidation(object_);
                if (!storedHClasses.has_value() || !ContainsSameHClasses(storedHClasses.value(), hclasses_)) {
                    return HClassCheckResult::FAILURE;
                }
            }
            std::optional<std::vector<RequestedHClassInfo>> requestedHClasses = TryBuildRequestedHClasses();
            if (!requestedHClasses.has_value()) {
                return HClassCheckResult::FAILURE;
            }
            KnownHClassesMerger merger(facts_, object_, requestedHClasses.value());
            if (!merger.InputIsValid()) {
                return HClassCheckResult::FAILURE;
            }
            if (merger.RequestedSetIsEmpty()) {
                return builder_->EmitUnconditionalHClassDeopt(bcIndex);
            }
            HClassCheckResult emissionResult =
                builder_->EmitHClassCheck(bcIndex, object_, merger.RequestedHClasses(), merger);
            if (emissionResult != HClassCheckResult::SUCCESS) {
                return emissionResult;
            }
            if (hclassesComeFromConstant_) {
                facts_->RecordPossibleHClasses(object_, hclasses_,
                                               builder_->IsHClassStableForFacts(hclasses_.front(), false));
            } else {
                facts_->MarkPossibleHClassesFresh(object_);
            }
            allHClassesAreFresh_ = true;
            return HClassCheckResult::SUCCESS;
        }

    private:
        std::optional<std::vector<RequestedHClassInfo>> TryBuildRequestedHClasses() const
        {
            ArkSteedHeapBroker *broker = builder_->self->pgoContext_.GetBroker();
            if (broker == nullptr) {
                return std::nullopt;
            }
            std::vector<RequestedHClassInfo> requestedHClasses;
            requestedHClasses.reserve(hclasses_.size());

            for (JSHClass *hclass : hclasses_) {
                ArkSteedHClassRef hclassRef = broker->MakeHClassRef(hclass);
                if (!hclassRef.IsSafeForCompile()) {
                    return std::nullopt;
                }
                requestedHClasses.push_back(RequestedHClassInfo {hclass, hclassRef, false});
            }
            return requestedHClasses;
        }

        static bool ContainsSameHClasses(const NodeInfo::PossibleHClasses &lhs, const NodeInfo::PossibleHClasses &rhs)
        {
            if (lhs.size() != rhs.size()) {
                return false;
            }
            return std::all_of(lhs.begin(), lhs.end(), [&rhs](JSHClass *hclass) {
                return std::find(rhs.begin(), rhs.end(), hclass) != rhs.end();
            });
        }

        BytecodeVisitor *builder_ {nullptr};
        ValueVertex *object_ {nullptr};
        CompileInfoFacts *facts_ {nullptr};
        NodeInfo::PossibleHClasses hclasses_;
        uint32_t effectEpoch_ {0};
        bool allHClassesAreFresh_ {false};
        bool hclassesComeFromConstant_ {false};
    };

    ValueVertex *BuildLoadField(ValueVertex *object, PropertyLookupResult plr)
    {
        auto convertHoleToUndefined = [this, plr](ValueVertex *value) -> ValueVertex * {
            if (plr.IsNotHole() || !plr.IsLoadFromIterResult()) {
                return value;
            }
            return self->NewVertex<ConvertHoleToUndefinedVertex>(compileInfoFacts_, currentBlock, {value});
        };
        if (plr.IsInlinedProps()) {
            int32_t offset = static_cast<int32_t>(plr.GetOffset());
            ValueVertex *result =
                self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {object}, offset);
            return convertHoleToUndefined(result);
        }
        ValueVertex *properties = self->NewVertex<LoadTaggedFieldVertex>(
            compileInfoFacts_, currentBlock, {object}, static_cast<int32_t>(JSObject::PROPERTIES_OFFSET));
        int32_t offset =
            static_cast<int32_t>(TaggedArray::DATA_OFFSET + plr.GetOffset() * JSTaggedValue::TaggedTypeSize());
        ValueVertex *result =
            self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {properties}, offset);
        return convertHoleToUndefined(result);
    }

    ValueVertex *BuildLoadFieldWithoutCse(ValueVertex *object, PropertyLookupResult plr)
    {
        if (plr.IsInlinedProps()) {
            int32_t offset = static_cast<int32_t>(plr.GetOffset());
            return self->NewVertex<LoadTaggedFieldVertex>(currentBlock, {object}, offset);
        }
        ValueVertex *properties = self->NewVertex<LoadTaggedFieldVertex>(
            currentBlock, {object}, static_cast<int32_t>(JSObject::PROPERTIES_OFFSET));
        int32_t offset =
            static_cast<int32_t>(TaggedArray::DATA_OFFSET + plr.GetOffset() * JSTaggedValue::TaggedTypeSize());
        return self->NewVertex<LoadTaggedFieldVertex>(currentBlock, {properties}, offset);
    }

    void BuildDeoptIfNotHeapObject(ValueVertex *value)
    {
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        self->NewVertex<DeoptIfNotHeapObjectVertex>(currentBlock, {value}, self->chunk_,
                                                    self->preproc_->GetBytecodeOffset(currentBcIndex))
            ->SetEagerDeoptFrameState(std::move(deoptFrameState));
    }

    void BuildDeoptIfArrayBufferDetached(ValueVertex *receiver, OnHeapMode onHeapMode)
    {
        if (OnHeap::IsOnHeap(onHeapMode)) {
            return;
        }
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        self->NewVertex<DeoptIfArrayBufferDetachedVertex>(currentBlock, {receiver}, self->chunk_,
                                                          self->preproc_->GetBytecodeOffset(currentBcIndex), onHeapMode)
            ->SetEagerDeoptFrameState(std::move(deoptFrameState));
    }

    void BuildDeoptIfCOWElements(ValueVertex *elements)
    {
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        self->NewVertex<DeoptIfCOWElementsVertex>(currentBlock, {elements}, self->chunk_,
                                                  self->preproc_->GetBytecodeOffset(currentBcIndex))
            ->SetEagerDeoptFrameState(std::move(deoptFrameState));
    }

    void BuildDeoptIfElementsUnstable(ValueVertex *receiver)
    {
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(currentBcIndex);
        self->NewVertex<DeoptIfElementsUnstableVertex>(currentBlock, {receiver}, self->chunk_,
                                                       self->preproc_->GetBytecodeOffset(currentBcIndex))
            ->SetEagerDeoptFrameState(std::move(deoptFrameState));
    }

    void BuildStoreTaggedElement(ValueVertex *elements, ValueVertex *index, ValueVertex *value,
                                 ArkSteedWriteBarrierValueKind provenValueKind = ArkSteedWriteBarrierValueKind::Unknown)
    {
        ArkSteedWriteBarrierValueKind valueKind = provenValueKind == ArkSteedWriteBarrierValueKind::Unknown
                                                      ? ClassifyDirectWriteBarrierValueKind(value)
                                                      : provenValueKind;
        if (valueKind == ArkSteedWriteBarrierValueKind::NonHeap) {
            self->NewVertex<StoreTaggedElementVertex>(compileInfoFacts_, currentBlock, {elements, index, value});
            return;
        }
        self->NewVertex<StoreTaggedElementWithBarrierVertex>(compileInfoFacts_, currentBlock,
                                                             {glue, elements, index, value}, valueKind);
    }

    ValueVertex *BuildCheckedElementIndex(ValueVertex *key)
    {
        if (compileInfoFacts_->CheckType(key, NodeInfo::NodeType::INT)) {
            return BuildTaggedIntToI32(key);
        }
        ValueVertex *keyF64 = BuildCheckedNumberToF64(key);
        ValueVertex *index = self->NewVertex<F64ToI32TruncVertex>(compileInfoFacts_, currentBlock,
                                                                  std::initializer_list<ValueVertex *> {keyF64});
        ValueVertex *roundTrip = self->NewVertex<I32ToF64Vertex>(compileInfoFacts_, currentBlock,
                                                                 std::initializer_list<ValueVertex *> {index});
        BuildDeoptIfFloat64Condition(keyF64, roundTrip, Condition::NOT_EQUAL, kungfu::DeoptType::NOTINT7);
        return index;
    }

    struct ResolvedElementStoreHClass {
        JSHClass *hclass {nullptr};
        ArkSteedHClassRef ref {};
    };

    struct ResolvedElementStoreTransitionGroup {
        ResolvedElementStoreHClass target {};
        ValueVertex *targetHClassConstant {nullptr};
        std::vector<ResolvedElementStoreHClass> transitionSources {};
        ElementsKind targetElementsKind {ElementsKind::NONE};
        bool useExactHClassTransition {false};
    };

    HClassCheckResult BuildCheckElementStoreHClasses(uint32_t bcIndex, ValueVertex *receiver,
                                                     const std::vector<ResolvedElementStoreHClass> &expectedHClasses)
    {
        if (expectedHClasses.empty()) {
            return HClassCheckResult::FAILURE;
        }

        std::vector<JSHClass *> rawHClasses;
        std::vector<ArkSteedHClassRef> hclassRefs;
        rawHClasses.reserve(expectedHClasses.size());
        hclassRefs.reserve(expectedHClasses.size());
        for (const ResolvedElementStoreHClass &expected : expectedHClasses) {
            if (expected.hclass == nullptr || !expected.ref.IsSafeForCompile()) {
                return HClassCheckResult::FAILURE;
            }
            rawHClasses.push_back(expected.hclass);
            hclassRefs.push_back(expected.ref);
        }
        return BuildCheckAnyOfHClasses(bcIndex, receiver, rawHClasses, hclassRefs, false, true);
    }

    bool ResolveElementStoreTransitionGroups(const ElementStoreAccessInfo &access,
                                             std::vector<ResolvedElementStoreTransitionGroup> *resolvedGroups)
    {
        if (!access.IsJSArray() || access.transitionGroupCount == 0) {
            return false;
        }

        std::vector<JSHClass *> seenHClasses;
        seenHClasses.reserve(access.caseCount);
        resolvedGroups->reserve(access.transitionGroupCount);
        for (uint32_t groupIndex = 0; groupIndex < access.transitionGroupCount; ++groupIndex) {
            const ElementStoreTransitionGroup &group = access.transitionGroups[groupIndex];
            ResolvedElementStoreTransitionGroup resolved;
            resolved.target.ref = group.targetHClass;
            if (!TryResolveHClassRef(group.targetHClass, &resolved.target.hclass) ||
                !resolved.target.hclass->IsJSArray() || group.targetElementsKind == ElementsKind::NONE ||
                resolved.target.hclass->GetElementsKind() != group.targetElementsKind ||
                std::find(seenHClasses.begin(), seenHClasses.end(), resolved.target.hclass) != seenHClasses.end()) {
                return false;
            }
            seenHClasses.push_back(resolved.target.hclass);
            resolved.targetElementsKind = group.targetElementsKind;
            resolved.useExactHClassTransition = group.useExactHClassTransition;
            if (resolved.useExactHClassTransition) {
                resolved.targetHClassConstant = GetHeapConstant(resolved.target.ref);
                if (resolved.targetHClassConstant == nullptr) {
                    return false;
                }
            }
            resolved.transitionSources.reserve(group.sourceCount);
            for (uint32_t sourceIndex = 0; sourceIndex < group.sourceCount; ++sourceIndex) {
                JSHClass *sourceHClass = nullptr;
                if (!TryResolveHClassRef(group.transitionSources[sourceIndex], &sourceHClass) ||
                    !sourceHClass->IsJSArray() ||
                    std::find(seenHClasses.begin(), seenHClasses.end(), sourceHClass) != seenHClasses.end()) {
                    return false;
                }
                seenHClasses.push_back(sourceHClass);
                resolved.transitionSources.push_back(
                    ResolvedElementStoreHClass {sourceHClass, group.transitionSources[sourceIndex]});
            }
            resolvedGroups->push_back(std::move(resolved));
        }
        return !seenHClasses.empty();
    }

    bool TryExpandAppendTransitionSources(std::vector<ResolvedElementStoreTransitionGroup> *groups)
    {
        for (ResolvedElementStoreTransitionGroup &group : *groups) {
            std::vector<ResolvedElementStoreHClass> expanded = group.transitionSources;
            if (expanded.empty()) {
                expanded.emplace_back(group.target);
            }
            std::vector<ResolvedElementStoreHClass> additions;
            for (const ResolvedElementStoreHClass &source : expanded) {
                ArkSteedHClassRef genericRef;
                JSHClass *generic = TryGetGenericInitialArrayHClass(source.hclass, &genericRef);
                if (generic == nullptr) {
                    continue;
                }
                bool alreadyAccepted = generic == group.target.hclass ||
                    std::any_of(group.transitionSources.begin(), group.transitionSources.end(),
                                [generic](const ResolvedElementStoreHClass &candidate) {
                                    return candidate.hclass == generic;
                                }) ||
                    std::any_of(additions.begin(), additions.end(),
                                [generic](const ResolvedElementStoreHClass &candidate) {
                                    return candidate.hclass == generic;
                                });
                if (!alreadyAccepted) {
                    additions.push_back(ResolvedElementStoreHClass {generic, genericRef});
                }
            }
            group.transitionSources.insert(group.transitionSources.end(), additions.begin(), additions.end());
        }
        return true;
    }

    template <class VertexT, class... Args>
    VertexT *NewElementsKindTransition(const NodeInfo::PossibleHClasses &sourceHClasses,
                                       std::initializer_list<ValueVertex *> inputs, Args &&...args)
    {
        static_assert(CanWrite(VertexT::PROPERTIES));
        VertexT *vertex = self->NewVertexImpl<VertexT>(currentBlock, inputs, std::forward<Args>(args)...);
        SideEffectDescriptor effect {SideEffectKind::ELEMENTS_KIND_TRANSITION};
        effect.transitionSources = &sourceHClasses;
        compileInfoFacts_->MarkPossibleSideEffect(effect, self->CurrentLoopEffects());
        return vertex;
    }

    void BuildTransitionElementsKind(ValueVertex *receiver, const ResolvedElementStoreTransitionGroup &group,
                                     JSHClass *knownSourceHClass = nullptr)
    {
        NodeInfo::PossibleHClasses sourceHClasses;
        if (knownSourceHClass != nullptr) {
            sourceHClasses.push_back(knownSourceHClass);
        } else {
            sourceHClasses.reserve(group.transitionSources.size());
            for (const ResolvedElementStoreHClass &source : group.transitionSources) {
                sourceHClasses.push_back(source.hclass);
            }
        }
        if (group.useExactHClassTransition) {
            ASSERT(group.targetHClassConstant != nullptr);
            NewElementsKindTransition<TransitionHClassWithBarrierVertex>(
                sourceHClasses, {glue, receiver, group.targetHClassConstant});
            compileInfoFacts_->RecordHClass(receiver, group.target.hclass, false);
            return;
        }
        // Mutant backing is rejected by the access-info factory, so changing to the canonical array HClass is enough.
        auto *call = NewElementsKindTransition<CallRuntimeVertex>(
            sourceHClasses, {receiver, TaggedConstantFromInt32(static_cast<int32_t>(group.targetElementsKind))},
            RTSTUB_ID(UpdateHClassForElementsKind));
        UpdateCatchBlockData(call);
        compileInfoFacts_->RecordHClass(receiver, group.target.hclass, false);
    }

    ValueVertex *BuildLoadJSArrayLength(ValueVertex *receiver)
    {
        if (ValueVertex *length = compileInfoFacts_->LookupLoadedProperty(receiver, PropertyKey::Length())) {
            return length;
        }
        ValueVertex *length = self->NewVertex<LoadInt32FieldVertex>(
            compileInfoFacts_, currentBlock, {receiver}, static_cast<int32_t>(JSArray::LENGTH_OFFSET));
        compileInfoFacts_->RecordLoadedProperty(receiver, PropertyKey::Length(), length);
        return length;
    }

    ValueVertex *BuildLoadTypedArrayLength(ValueVertex *receiver, JSType type)
    {
        bool isFixedLength = IsFixedLengthTypedArrayType(type);
        if (isFixedLength) {
            if (ValueVertex *length = compileInfoFacts_->LookupLoadedConstantProperty(
                    receiver, PropertyKey::TypedArrayLength())) {
                return length;
            }
        }
        ValueVertex *length = self->NewVertex<LoadInt32FieldVertex>(
            compileInfoFacts_, currentBlock, {receiver}, static_cast<int32_t>(JSTypedArray::ARRAY_LENGTH_OFFSET));
        if (isFixedLength) {
            compileInfoFacts_->RecordLoadedConstantProperty(receiver, PropertyKey::TypedArrayLength(), length);
        }
        return length;
    }

    ValueVertex *BuildLoadElements(ValueVertex *receiver)
    {
        if (ValueVertex *elements = compileInfoFacts_->LookupLoadedProperty(receiver, PropertyKey::Elements())) {
            return elements;
        }
        ValueVertex *elements = self->NewVertex<LoadTaggedFieldVertex>(
            compileInfoFacts_, currentBlock, {receiver}, static_cast<int32_t>(JSObject::ELEMENTS_OFFSET));
        compileInfoFacts_->RecordLoadedProperty(receiver, PropertyKey::Elements(), elements);
        return elements;
    }

    void BuildJSArrayElementAppend(ValueVertex *receiver, ValueVertex *index, ValueVertex *value,
                                   ElementsKind elementsKind, CommonStubID fallbackStubID)
    {
        std::optional<int32_t> constantIndex = TryGetInt32Value(index);
        ASSERT(constantIndex.has_value());

        BuildDeoptIfElementsUnstable(receiver);
        ValueVertex *length = self->NewVertex<LoadInt32FieldVertex>(
            compileInfoFacts_, currentBlock, {receiver}, static_cast<int32_t>(JSArray::LENGTH_OFFSET));
        BuildDeoptIfInt32Condition(index, length, Condition::LESS_THAN, kungfu::DeoptType::NOTLEGALIDX1);

        ValueVertex *storedValue = value;
        ArkSteedWriteBarrierValueKind provenValueKind = ArkSteedWriteBarrierValueKind::Unknown;
        if (Elements::IsIntOrHoleInt(elementsKind)) {
            BuildCheckedTaggedIntToI32(value);
            provenValueKind = ArkSteedWriteBarrierValueKind::NonHeap;
        } else if (Elements::IsNumberOrHoleNumber(elementsKind)) {
            BuildCheckedNumberToF64(value);
            provenValueKind = ArkSteedWriteBarrierValueKind::NonHeap;
        } else if (Elements::IsStringOrHoleString(elementsKind)) {
            storedValue = BuildCheckedTaggedString(value);
            provenValueKind = ArkSteedWriteBarrierValueKind::HeapObject;
        } else if (elementsKind == ElementsKind::OBJECT || elementsKind == ElementsKind::HOLE_OBJECT) {
            BuildDeoptIfNotHeapObject(value);
            provenValueKind = ArkSteedWriteBarrierValueKind::HeapObject;
        }

        ValueVertex *elements = self->NewVertex<LoadTaggedFieldVertex>(
            compileInfoFacts_, currentBlock, {receiver}, static_cast<int32_t>(JSObject::ELEMENTS_OFFSET));
        ValueVertex *capacity = self->NewVertex<LoadInt32FieldVertex>(
            compileInfoFacts_, currentBlock, {elements}, static_cast<int32_t>(TaggedArray::LENGTH_OFFSET));

        CompileInfoFacts *entryFacts = compileInfoFacts_;
        BB *inlineBlock = self->NewBlock();
        BB *growBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();
        growBlock->SetDeferred(true);
        self->FinishBlockWithBranch<BranchIfInt32CompareVertex>(
            currentBlock, {index, capacity}, inlineBlock, growBlock, Condition::BELOW);

        currentBlock = inlineBlock;
        compileInfoFacts_ = entryFacts->Clone();
        BuildDeoptIfCOWElements(elements);
        BuildStoreTaggedElement(elements, index, storedValue, provenValueKind);
        // Inline path performs the store in-place and updates the length itself.
        self->NewVertex<StoreInt32FieldVertex>(
            compileInfoFacts_, currentBlock, {receiver, self->graph_->GetInt32Constant(*constantIndex + 1)},
            static_cast<int32_t>(JSArray::LENGTH_OFFSET));
        self->FinishBlockWithJump(currentBlock, doneBlock);
        CompileInfoFacts *inlineExitFacts = compileInfoFacts_->Clone();

        currentBlock = growBlock;
        compileInfoFacts_ = entryFacts->Clone();
        // Hand off the entire store process (grow + element store + length update) to the
        // runtime stub. The stub matches the slow-path signature of the owning bytecode.
        CommonStubCallWithLazyDeopt({glue, receiver, index, value, GlobalEnv()}, fallbackStubID);
        self->FinishBlockWithJump(currentBlock, doneBlock);
        CompileInfoFacts *growExitFacts = compileInfoFacts_;

        currentBlock = doneBlock;
        compileInfoFacts_ = inlineExitFacts;
        compileInfoFacts_->Merge(*growExitFacts);
    }

    ValueVertex *BuildEnsureWritableElements(ValueVertex *receiver, ValueVertex *elements)
    {
        ValueVertex *writableElements = self->NewVertex<EnsureWritableElementsVertex>(
            compileInfoFacts_, currentBlock, {elements, receiver});
        compileInfoFacts_->RecordLoadedProperty(receiver, PropertyKey::Elements(), writableElements);
        return writableElements;
    }

    void BuildJSArrayElementStore(ValueVertex *receiver, ValueVertex *index, ValueVertex *value,
                                  ElementsKind elementsKind, bool handlesCOW)
    {
        BuildDeoptIfElementsUnstable(receiver);
        ValueVertex *length = BuildLoadJSArrayLength(receiver);
        BuildDeoptIfInt32Condition(index, length, Condition::GREATER_THAN_OR_EQUAL,
                                   kungfu::DeoptType::NOTLEGALIDX1);
        ValueVertex *elements = BuildLoadElements(receiver);
        if (handlesCOW) {
            elements = BuildEnsureWritableElements(receiver, elements);
        } else {
            BuildDeoptIfCOWElements(elements);
        }
        if (Elements::IsIntOrHoleInt(elementsKind)) {
            BuildCheckedTaggedIntToI32(value);
            BuildStoreTaggedElement(elements, index, value, ArkSteedWriteBarrierValueKind::NonHeap);
            return;
        }
        if (Elements::IsNumberOrHoleNumber(elementsKind)) {
            BuildCheckedNumberToF64(value);
            BuildStoreTaggedElement(elements, index, value, ArkSteedWriteBarrierValueKind::NonHeap);
            return;
        }
        if (Elements::IsStringOrHoleString(elementsKind)) {
            ValueVertex *stringValue = BuildCheckedTaggedString(value);
            BuildStoreTaggedElement(elements, index, stringValue, ArkSteedWriteBarrierValueKind::HeapObject);
            return;
        }
        bool objectElements = elementsKind == ElementsKind::OBJECT || elementsKind == ElementsKind::HOLE_OBJECT;
        if (objectElements) {
            BuildDeoptIfNotHeapObject(value);
        }
        BuildStoreTaggedElement(elements, index, value,
                                objectElements ? ArkSteedWriteBarrierValueKind::HeapObject
                                               : ArkSteedWriteBarrierValueKind::Unknown);
    }

    bool BuildJSArrayElementStoreInBoundsDispatch(const ElementStoreAccessInfo &access,
                                                  const std::vector<ResolvedElementStoreTransitionGroup> &groups,
                                                  JSHClass *knownHClass, ValueVertex *receiver, ValueVertex *index,
                                                  ValueVertex *value)
    {
        return BuildJSArrayElementStoreDispatchImpl(
            access, groups, knownHClass, receiver, index, value,
            [this, handlesCOW = access.handlesCOW](ValueVertex *receiver, ValueVertex *index,
                                                  ValueVertex *value, ElementsKind kind) {
                BuildJSArrayElementStore(receiver, index, value, kind, handlesCOW);
            });
    }

    bool BuildJSArrayElementAppendDispatch(const ElementStoreAccessInfo &access,
                                           const std::vector<ResolvedElementStoreTransitionGroup> &groups,
                                           JSHClass *knownHClass, ValueVertex *receiver, ValueVertex *index,
                                           ValueVertex *value, CommonStubID fallbackStubID)
    {
        return BuildJSArrayElementStoreDispatchImpl(
            access, groups, knownHClass, receiver, index, value,
            [this, fallbackStubID](ValueVertex *receiver, ValueVertex *index, ValueVertex *value, ElementsKind kind) {
                BuildJSArrayElementAppend(receiver, index, value, kind, fallbackStubID);
            });
    }

    template <typename StoreAction>
    bool BuildJSArrayElementStoreDispatchImpl(const ElementStoreAccessInfo &access,
                                              const std::vector<ResolvedElementStoreTransitionGroup> &groups,
                                              JSHClass *knownHClass, ValueVertex *receiver, ValueVertex *index,
                                              ValueVertex *value, StoreAction storeAction)
    {
        if (knownHClass != nullptr) {
            for (const ResolvedElementStoreTransitionGroup &group : groups) {
                if (knownHClass == group.target.hclass) {
                    storeAction(receiver, index, value, group.targetElementsKind);
                    return true;
                }
                auto source = std::find_if(group.transitionSources.begin(), group.transitionSources.end(),
                                           [knownHClass](const ResolvedElementStoreHClass &candidate) {
                                               return candidate.hclass == knownHClass;
                                           });
                if (source != group.transitionSources.end()) {
                    BuildTransitionElementsKind(receiver, group, source->hclass);
                    storeAction(receiver, index, value, group.targetElementsKind);
                    return true;
                }
            }
            UNREACHABLE();
        }

        struct DispatchEntry {
            const ResolvedElementStoreHClass *hclass {nullptr};
            uint32_t hclassHandleIndex {0};
            uint32_t groupIndex {0};
            bool needsTransition {false};
        };
        std::vector<DispatchEntry> entries;
        entries.reserve(access.caseCount + groups.size());
        for (uint32_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            entries.push_back(DispatchEntry {&groups[groupIndex].target, 0, groupIndex, false});
            for (const ResolvedElementStoreHClass &source : groups[groupIndex].transitionSources) {
                entries.push_back(DispatchEntry {&source, 0, groupIndex, true});
            }
        }
        if (entries.empty()) {
            UNREACHABLE();
        }
        for (DispatchEntry &entry : entries) {
            std::optional<uint32_t> handleIndex = GetHeapConstantHandleIndex(entry.hclass->ref);
            if (!handleIndex.has_value()) {
                return false;
            }
            entry.hclassHandleIndex = handleIndex.value();
        }

        CompileInfoFacts *entryFacts = compileInfoFacts_;
        std::vector<BB *> checkBlocks;
        checkBlocks.reserve(entries.size());
        for (uint32_t i = 0; i < entries.size(); ++i) {
            checkBlocks.push_back(self->NewBlock());
        }
        std::vector<BB *> actionBlocks;
        std::vector<BB *> transitionBlocks;
        actionBlocks.reserve(groups.size());
        transitionBlocks.reserve(groups.size());
        for (const ResolvedElementStoreTransitionGroup &group : groups) {
            actionBlocks.push_back(self->NewBlock());
            transitionBlocks.push_back(group.transitionSources.empty() ? nullptr : self->NewBlock());
        }
        std::vector<BB *> dispatchLandingBlocks;
        dispatchLandingBlocks.reserve(entries.size());
        // Keep conditional dispatch edges out of shared action blocks so RA can emit edge moves on the landing jump.
        for (uint32_t i = 0; i < entries.size(); ++i) {
            dispatchLandingBlocks.push_back(self->NewBlock());
        }
        BB *primitiveDeoptBlock = self->NewBlock();
        BB *hclassMissDeoptBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();
        primitiveDeoptBlock->SetDeferred(true);
        hclassMissDeoptBlock->SetDeferred(true);

        self->FinishBlockWithBranch<BranchIfTaggedHeapObjectVertex>(currentBlock, {receiver}, checkBlocks.front(),
                                                                    primitiveDeoptBlock);

        ValueVertex *actualHClass = nullptr;
        for (uint32_t i = 0; i < entries.size(); ++i) {
            currentBlock = checkBlocks[i];
            compileInfoFacts_ = entryFacts;
            if (actualHClass == nullptr) {
                actualHClass = self->NewVertex<LoadHClassAddressVertex>(
                    compileInfoFacts_, currentBlock, std::initializer_list<ValueVertex *> {receiver});
            }
            BB *nextBlock = i + 1 < entries.size() ? checkBlocks[i + 1] : hclassMissDeoptBlock;
            self->FinishBlockWithBranch<BranchIfHClassInVertex>(
                currentBlock, {actualHClass}, dispatchLandingBlocks[i], nextBlock, self->chunk_,
                std::vector<uint32_t> {entries[i].hclassHandleIndex}, true);
        }

        for (uint32_t i = 0; i < entries.size(); ++i) {
            currentBlock = dispatchLandingBlocks[i];
            compileInfoFacts_ = entryFacts;
            BB *destination = entries[i].needsTransition ? transitionBlocks[entries[i].groupIndex]
                                                         : actionBlocks[entries[i].groupIndex];
            self->FinishBlockWithJump(currentBlock, destination);
        }

        CompileInfoFacts *mergedExitFacts = nullptr;
        for (uint32_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            CompileInfoFacts *actionEntryFacts = entryFacts->Clone();
            if (transitionBlocks[groupIndex] != nullptr) {
                currentBlock = transitionBlocks[groupIndex];
                compileInfoFacts_ = entryFacts->Clone();
                BuildTransitionElementsKind(receiver, groups[groupIndex]);
                actionEntryFacts->Merge(*compileInfoFacts_);
                self->FinishBlockWithJump(currentBlock, actionBlocks[groupIndex]);
            }

            currentBlock = actionBlocks[groupIndex];
            compileInfoFacts_ = actionEntryFacts;
            // Both the target-map edge and the completed transition reach this block with the target HClass.
            compileInfoFacts_->RecordHClass(receiver, groups[groupIndex].target.hclass, false);
            storeAction(receiver, index, value, groups[groupIndex].targetElementsKind);
            self->FinishBlockWithJump(currentBlock, doneBlock);
            if (mergedExitFacts == nullptr) {
                mergedExitFacts = compileInfoFacts_->Clone();
            } else {
                mergedExitFacts->Merge(*compileInfoFacts_);
            }
        }

        auto buildDeoptBlock = [&](BB *deoptBlock) {
            currentBlock = deoptBlock;
            compileInfoFacts_ = entryFacts->Clone();
            auto *deopt =
                self->FinishBlockWith<DeoptVertex>(currentBlock, {}, self->chunk_, kungfu::DeoptType::KEYMISSMATCH,
                                                   self->preproc_->GetBytecodeOffset(currentBcIndex));
            deopt->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(currentBcIndex));
        };
        buildDeoptBlock(primitiveDeoptBlock);
        buildDeoptBlock(hclassMissDeoptBlock);

        ASSERT(mergedExitFacts != nullptr);
        currentBlock = doneBlock;
        compileInfoFacts_ = mergedExitFacts;
        return true;
    }

    // Find a known HClass among the transition groups. Returns nullptr if `receiver` is not known
    // to be one of the target / source HClasses of any group.
    JSHClass *FindKnownJSArrayHClass(
        ValueVertex *receiver, const std::vector<ResolvedElementStoreTransitionGroup> &groups)
    {
        JSHClass *knownJSArrayHClass = TryGetFreshKnownHClass(receiver);
        if (knownJSArrayHClass == nullptr) {
            return nullptr;
        }
        for (const ResolvedElementStoreTransitionGroup &group : groups) {
            if (knownJSArrayHClass == group.target.hclass) {
                return knownJSArrayHClass;
            }
            if (std::any_of(group.transitionSources.begin(), group.transitionSources.end(),
                            [knownJSArrayHClass](const ResolvedElementStoreHClass &source) {
                                return source.hclass == knownJSArrayHClass;
                            })) {
                return knownJSArrayHClass;
            }
        }
        return nullptr;
    }

    bool TryLowerElementStoreInBounds(uint32_t bcIndex, const ElementStoreAccessInfo &access,
                                      ValueVertex *receiver, ValueVertex *key, ValueVertex *value)
    {
        if ((!access.IsJSArray() && !access.IsTypedArray()) || access.caseCount == 0) {
            return false;
        }

        if (access.IsTypedArray()) {
            return TryLowerElementStoreTypedArray(bcIndex, access, receiver, key, value);
        }
        std::vector<ResolvedElementStoreTransitionGroup> jsArrayGroups;
        if (!ResolveElementStoreTransitionGroups(access, &jsArrayGroups)) {
            return false;
        }
        JSHClass *knownJSArrayHClass = FindKnownJSArrayHClass(receiver, jsArrayGroups);
        ValueVertex *index = BuildCheckedNonNegativeIndex(key);
        return BuildJSArrayElementStoreInBoundsDispatch(
            access, jsArrayGroups, knownJSArrayHClass, receiver, index, value);
    }

    bool TryLowerElementStoreAppend(uint32_t bcIndex, const ElementStoreAccessInfo &access,
                                    ValueVertex *receiver, ValueVertex *key, ValueVertex *value,
                                    CommonStubID fallbackStubID)
    {
        if (!access.IsJSArray() || access.caseCount == 0) {
            return false;
        }

        std::vector<ResolvedElementStoreTransitionGroup> jsArrayGroups;
        if (!ResolveElementStoreTransitionGroups(access, &jsArrayGroups)) {
            return false;
        }
        if (!TryExpandAppendTransitionSources(&jsArrayGroups)) {
            return false;
        }
        JSHClass *knownJSArrayHClass = FindKnownJSArrayHClass(receiver, jsArrayGroups);
        ValueVertex *index = BuildCheckedNonNegativeIndex(key);
        if (!TryGetInt32Value(index).has_value()) {
            return false;
        }
        return BuildJSArrayElementAppendDispatch(
            access, jsArrayGroups, knownJSArrayHClass, receiver, index, value, fallbackStubID);
    }

    ValueVertex *BuildCheckedNonNegativeIndex(ValueVertex *key)
    {
        ValueVertex *index = BuildCheckedElementIndex(key);
        ValueVertex *zero = self->graph_->GetInt32Constant(0);
        BuildDeoptIfInt32Condition(index, zero, Condition::LESS_THAN, kungfu::DeoptType::INDEXLESSZERO);
        return index;
    }

    bool TryLowerElementStoreTypedArray(uint32_t bcIndex, const ElementStoreAccessInfo &access,
                                        ValueVertex *receiver, ValueVertex *key, ValueVertex *value)
    {
        std::vector<ResolvedElementStoreHClass> receiverHClasses;
        receiverHClasses.reserve(access.caseCount);
        for (uint32_t i = 0; i < access.caseCount; ++i) {
            JSHClass *receiverHClass = nullptr;
            if (!TryResolveHClassRef(access.cases[i].expectedHClass, &receiverHClass) ||
                !receiverHClass->IsTypedArray() || receiverHClass->GetObjectType() != access.typedArrayType) {
                return false;
            }
            receiverHClasses.push_back(
                ResolvedElementStoreHClass {receiverHClass, access.cases[i].expectedHClass});
        }
        HClassCheckResult checkResult =
            BuildCheckElementStoreHClasses(bcIndex, receiver, receiverHClasses);
        if (checkResult != HClassCheckResult::SUCCESS) {
            return checkResult == HClassCheckResult::UNREACHABLE;
        }

        ValueVertex *index = BuildCheckedNonNegativeIndex(key);
        BuildDeoptIfArrayBufferDetached(receiver, access.onHeapMode);
        ValueVertex *length = BuildLoadTypedArrayLength(receiver, access.typedArrayType);
        BuildDeoptIfInt32Condition(index, length, Condition::GREATER_THAN_OR_EQUAL,
                                   kungfu::DeoptType::NOTLEGALIDX1);
        switch (access.typedArrayType) {
            case JSType::JS_INT8_ARRAY:
            case JSType::JS_UINT8_ARRAY:
            case JSType::JS_INT16_ARRAY:
            case JSType::JS_UINT16_ARRAY:
            case JSType::JS_INT32_ARRAY:
            case JSType::JS_UINT32_ARRAY: {
                BuildTruncatingNumberTypedArrayStore(receiver, index, value, access);
                return true;
            }
            case JSType::JS_UINT8_CLAMPED_ARRAY:
                BuildClampedUint8TypedArrayStore(receiver, index, value, access);
                return true;
            case JSType::JS_FLOAT32_ARRAY:
            case JSType::JS_FLOAT64_ARRAY: {
                ValueVertex *doubleValue = BuildCheckedNumberToF64(value);
                self->NewVertex<StoreFloatTypedArrayElementVertex>(compileInfoFacts_, currentBlock,
                                                                   {receiver, index, doubleValue},
                                                                   access.typedArrayType, access.onHeapMode);
                return true;
            }
            default:
                UNREACHABLE();
        }
    }

    template <class VertexT, class InputRange = std::initializer_list<ValueVertex *>, class... Args>
    VertexT *NewFieldStore(PropertyKey key, const InputRange &inputs, Args &&...args)
    {
        static_assert(CanWrite(VertexT::PROPERTIES));
        VertexT *vertex = self->NewVertexImpl<VertexT>(currentBlock, inputs, std::forward<Args>(args)...);
        SideEffectDescriptor effect = ArkSteedSideEffectClassifier::Classify(vertex);
        ASSERT(effect.kind == SideEffectKind::FIELD_WRITE);
        effect.propertyKey = key;
        compileInfoFacts_->MarkPossibleSideEffect(effect, self->CurrentLoopEffects());
        return vertex;
    }

    void BuildStoreTaggedField(ValueVertex *object, int32_t offset, ValueVertex *value,
                               PropertyKey key = PropertyKey::Unknown(),
                               ArkSteedWriteBarrierValueKind provenValueKind = ArkSteedWriteBarrierValueKind::Unknown)
    {
        ArkSteedWriteBarrierValueKind valueKind = provenValueKind == ArkSteedWriteBarrierValueKind::Unknown
                                                      ? ClassifyDirectWriteBarrierValueKind(value)
                                                      : provenValueKind;
        if (valueKind == ArkSteedWriteBarrierValueKind::NonHeap) {
            NewFieldStore<StoreTaggedFieldVertex>(key, {object, value}, offset);
            return;
        }
        NewFieldStore<StoreTaggedFieldWithBarrierVertex>(key, {glue, object, value}, offset, valueKind);
    }

    void BuildStoreField(ValueVertex *object, ValueVertex *value, PropertyLookupResult plr, PropertyKey key)
    {
        if (plr.IsInlinedProps()) {
            BuildStoreTaggedField(object, static_cast<int32_t>(plr.GetOffset()), value, key);
            return;
        }
        ValueVertex *properties = self->NewVertex<LoadTaggedFieldVertex>(
            compileInfoFacts_, currentBlock, {object}, static_cast<int32_t>(JSObject::PROPERTIES_OFFSET));
        int32_t offset = static_cast<int32_t>(
            TaggedArray::DATA_OFFSET + plr.GetOffset() * JSTaggedValue::TaggedTypeSize());
        BuildStoreTaggedField(properties, offset, value, key);
    }

    ValueVertex *BuildPropertyLoadSource(uint32_t bcIndex, ValueVertex *object, const NamedLoadAccessInfo &accessInfo)
    {
        if (accessInfo.holderDepth <= 0) {
            return object;
        }
        if (accessInfo.hasStableProtoChain) {
            if (accessInfo.holderHandleIndex == JitCompilationEnv::INVALID_HEAP_CONSTANT_INDEX ||
                !accessInfo.holderRef.IsSafeForCompile()) {
                return nullptr;
            }
            return GetHeapConstant(accessInfo.holderRef);
        }
        if (accessInfo.expectedPrototypeHClassRefs.size() != accessInfo.holderDepth) {
            return nullptr;
        }
        std::vector<uint32_t> expectedHClassHandleIndices;
        if (!GetHeapConstantHandleIndices(accessInfo.expectedPrototypeHClassRefs, &expectedHClassHandleIndices)) {
            return nullptr;
        }
        auto *loadHolder = self->NewVertex<LoadPrototypeHolderByHClassVertex>(
            compileInfoFacts_, currentBlock, {object}, self->chunk_, accessInfo.holderDepth,
            expectedHClassHandleIndices, self->preproc_->GetBytecodeOffset(bcIndex));
        loadHolder->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(bcIndex));
        return loadHolder;
    }

    bool TryResolveHClassRef(const ArkSteedHClassRef &ref, JSHClass **hclass) const
    {
        auto *broker = self->pgoContext_.GetBroker();
        if (hclass == nullptr || broker == nullptr) {
            return false;
        }
        *hclass = broker->TryGetHClass(ref);
        return *hclass != nullptr;
    }

    JSHClass *TryGetGenericInitialArrayHClass(JSHClass *receiverHClass, ArkSteedHClassRef *genericHClassRef) const
    {
        auto *broker = self->pgoContext_.GetBroker();
        return broker == nullptr ? nullptr : broker->TryGetGenericInitialArrayHClass(receiverHClass, genericHClassRef);
    }

    std::optional<uint32_t> GetHeapConstantHandleIndex(const ArkSteedHeapRef &ref) const
    {
        auto *broker = self->pgoContext_.GetBroker();
        uint32_t handleIndex = JitCompilationEnv::INVALID_HEAP_CONSTANT_INDEX;
        if (g_isEnableCMCGC || broker == nullptr || !broker->TryGetHeapConstantIndex(ref, &handleIndex)) {
            return std::nullopt;
        }
        return handleIndex;
    }

    std::optional<uint32_t> GetHeapConstantNameHandleIndex(const ArkSteedNameRef &ref) const
    {
        return ref.IsName() ? GetHeapConstantHandleIndex(ref) : std::nullopt;
    }

    bool GetHeapConstantHandleIndices(const std::vector<ArkSteedHClassRef> &refs,
                                      std::vector<uint32_t> *handleIndices) const
    {
        if (handleIndices == nullptr) {
            return false;
        }
        handleIndices->clear();
        handleIndices->reserve(refs.size());
        for (const ArkSteedHClassRef &ref : refs) {
            std::optional<uint32_t> handleIndex = GetHeapConstantHandleIndex(ref);
            if (!handleIndex.has_value()) {
                return false;
            }
            handleIndices->push_back(handleIndex.value());
        }
        return true;
    }

    ValueVertex *GetHeapConstant(const ArkSteedHeapRef &ref)
    {
        auto handleIndex = GetHeapConstantHandleIndex(ref);
        if (!handleIndex.has_value()) {
            return nullptr;
        }
        JSType type;
        if (!self->pgoContext_.GetBroker()->TryGetObjectType(ref, &type)) {
            return nullptr;
        }
        return self->graph_->GetHeapConstant(handleIndex.value(), static_cast<uint16_t>(NodeTypeFromJSType(type)));
    }

    JSHClass *TryGetFreshKnownHClass(ValueVertex *receiver) const
    {
        return compileInfoFacts_->TryGetHClass(receiver);
    }

    HClassCheckResult RequireKnownHClass(uint32_t bcIndex, const NamedStoreAccessInfo &access, ValueVertex *receiver,
                                         JSHClass *receiverHClass)
    {
        if (receiverHClass == nullptr) {
            return HClassCheckResult::FAILURE;
        }
        return BuildCheckSingleHClass(bcIndex, receiver, receiverHClass, access.expectedHClass, false,
                                      access.dependencies.canAssumeStableHClass);
    }

    bool TryLowerNamedStoreShared(uint32_t bcIndex, const NamedStoreAccessInfo &access, ValueVertex *receiver,
                                  ValueVertex *value, PropertyKey key)
    {
        JSHClass *receiverHClass = nullptr;
        if (access.mode != AccessMode::NAMED_STORE || !access.isSharedStore || !access.holderIsReceiver ||
            !TryResolveHClassRef(access.expectedHClass, &receiverHClass)) {
            return false;
        }
        HClassCheckResult checkResult = RequireKnownHClass(bcIndex, access, receiver, receiverHClass);
        if (checkResult != HClassCheckResult::SUCCESS) {
            return checkResult == HClassCheckResult::UNREACHABLE;
        }

        ValueVertex *storeTarget = receiver;
        if (access.fieldStorage == AccessFieldStorage::PROPERTIES_ARRAY) {
            storeTarget = self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {receiver},
                                                                 static_cast<int32_t>(JSObject::PROPERTIES_OFFSET));
        } else if (access.fieldStorage != AccessFieldStorage::IN_OBJECT) {
            return false;
        }

        auto *prepareField = self->NewVertex<PrepareSharedStoreFieldVertex>(
            compileInfoFacts_, currentBlock, std::initializer_list<ValueVertex *> {value}, access.handlerInfo);
        UpdateCatchBlockData(prepareField);
        LoadLazyDeoptFrameStateForThrowableCall(bcIndex, prepareField);
        NewFieldStore<StoreSharedFieldWithBarrierVertex>(
            key, std::initializer_list<ValueVertex *> {glue, storeTarget, prepareField}, access.fieldOffset);
        return true;
    }

    bool TryLowerNamedStoreAccessor(uint32_t bcIndex, const NamedStoreAccessInfo &access, ValueVertex *receiver,
                                    ValueVertex *value)
    {
        if (access.mode != AccessMode::NAMED_STORE || access.kind != AccessKind::ACCESSOR) {
            return false;
        }

        ValueVertex *holder = receiver;
        if (!access.holderIsReceiver) {
            holder = BuildPrototypeHolder(access);
            if (holder == nullptr) {
                return false;
            }
        }

        ValueVertex *accessorHolder = holder;
        if (access.fieldStorage == AccessFieldStorage::PROPERTIES_ARRAY) {
            accessorHolder = self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {holder},
                                                                    static_cast<int32_t>(JSObject::PROPERTIES_OFFSET));
        } else if (access.fieldStorage != AccessFieldStorage::IN_OBJECT) {
            return false;
        }
        ValueVertex *accessor = self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock,
                                                                       {accessorHolder}, access.fieldOffset);

        CompileInfoFacts *entryFacts = compileInfoFacts_;
        BB *internalAccessorBlock = self->NewBlock();
        BB *loadSetterBlock = self->NewBlock();
        BB *undefinedSetterBlock = self->NewBlock();
        BB *callSetterBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();
        internalAccessorBlock->SetDeferred(true);
        undefinedSetterBlock->SetDeferred(true);

        self->FinishBlockWithBranch<BranchIfObjectTypeVertex>(currentBlock, {accessor}, internalAccessorBlock,
                                                              loadSetterBlock, JSType::INTERNAL_ACCESSOR);

        currentBlock = internalAccessorBlock;
        compileInfoFacts_ = entryFacts->Clone();
        auto *internalCall = RuntimeCall({receiver, accessor, value}, RTSTUB_ID(CallInternalSetter));
        LoadLazyDeoptFrameStateForThrowableCall(bcIndex, internalCall);
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = loadSetterBlock;
        compileInfoFacts_ = entryFacts->Clone();
        ValueVertex *setter = self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {accessor},
                                                                     static_cast<int32_t>(AccessorData::SETTER_OFFSET));
        CompileInfoFacts *setterFacts = compileInfoFacts_;
        self->FinishBlockWithBranch<BranchIfReferenceEqualVertex>(currentBlock, {setter, self->undefinedValue_},
                                                                  undefinedSetterBlock, callSetterBlock);

        currentBlock = undefinedSetterBlock;
        compileInfoFacts_ = setterFacts->Clone();
        auto *throwCall = RuntimeCall({}, RTSTUB_ID(ThrowSetterIsUndefinedException));
        LoadLazyDeoptFrameStateForThrowableCall(bcIndex, throwCall);
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = callSetterBlock;
        compileInfoFacts_ = setterFacts->Clone();
        CallVertex *call = BuildGenericCallVertex(
            std::initializer_list<ValueVertex *> {setter, self->undefinedValue_, receiver, value}, 1);
        LoadLazyDeoptFrameStateForThrowableCall(bcIndex, call);
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = doneBlock;
        compileInfoFacts_ = entryFacts;
        compileInfoFacts_->OnSideEffect();
        return true;
    }

    bool BuildNamedStoreEagerGuards(uint32_t bcIndex, const NamedStoreAccessInfo &access, ValueVertex *receiver,
                                    bool checkNotPrototype = false)
    {
        bool needsStableProtoChain =
            access.hasProtoCell || !access.holderIsReceiver || access.kind == AccessKind::TRANSITION;
        bool needsProtoMarker = needsStableProtoChain && !access.dependencies.canAssumeStableProtoChain;
        checkNotPrototype = checkNotPrototype && !access.dependencies.canAssumeNotPrototype;
        if (!needsProtoMarker && !checkNotPrototype) {
            return true;
        }
        ChunkVector<ValueVertex *> guardInputs(self->chunk_);
        guardInputs.emplace_back(receiver);
        auto *guard = self->NewVertex<DeoptIfPrototypeChangedVertex>(currentBlock, guardInputs, self->chunk_,
                                                                     needsProtoMarker, checkNotPrototype,
                                                                     self->preproc_->GetBytecodeOffset(bcIndex));
        guard->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(bcIndex));
        return true;
    }

    ValueVertex *BuildPrototypeHolder(const NamedStoreAccessInfo &access)
    {
        return access.holder.IsSafeForCompile() ? GetHeapConstant(access.holder) : nullptr;
    }

    ValueVertex *BuildCheckedNamedStoreValue(AccessFieldRepresentation representation, ValueVertex *value)
    {
        switch (representation) {
            case AccessFieldRepresentation::TAGGED:
                return value;
            case AccessFieldRepresentation::INT32:
                return BuildCheckedTaggedIntToI32(value);
            case AccessFieldRepresentation::DOUBLE:
                return BuildCheckedNumberToF64(value);
            default:
                return nullptr;
        }
    }

    void BuildPreparedNamedStoreField(ValueVertex *storeTarget, int32_t offset, ValueVertex *value,
                                      AccessFieldRepresentation representation,
                                      PropertyKey key = PropertyKey::Unknown())
    {
        switch (representation) {
            case AccessFieldRepresentation::TAGGED:
                BuildStoreTaggedField(storeTarget, offset, value, key);
                return;
            case AccessFieldRepresentation::INT32:
                NewFieldStore<StoreInt32FieldVertex>(
                    key, std::initializer_list<ValueVertex *> {storeTarget, value}, offset);
                return;
            case AccessFieldRepresentation::DOUBLE:
                NewFieldStore<StoreDoubleFieldVertex>(
                    key, std::initializer_list<ValueVertex *> {storeTarget, value}, offset);
                return;
            default:
                UNREACHABLE();
        }
    }

    bool TryLowerNamedStoreTransition(uint32_t bcIndex, const NamedStoreAccessInfo &access, ValueVertex *receiver,
                                      ValueVertex *value)
    {
        JSHClass *receiverHClass = nullptr;
        JSHClass *transitionHClass = nullptr;
        bool supportedRepresentation = access.fieldRepresentation == AccessFieldRepresentation::TAGGED ||
                                       access.fieldRepresentation == AccessFieldRepresentation::INT32 ||
                                       access.fieldRepresentation == AccessFieldRepresentation::DOUBLE;
        if (access.mode != AccessMode::NAMED_STORE || access.kind != AccessKind::TRANSITION ||
            !access.holderIsReceiver || !supportedRepresentation ||
            !TryResolveHClassRef(access.expectedHClass, &receiverHClass) ||
            !TryResolveHClassRef(access.transitionHClass, &transitionHClass)) {
            return false;
        }
        if (access.fieldStorage != AccessFieldStorage::IN_OBJECT &&
            access.fieldStorage != AccessFieldStorage::PROPERTIES_ARRAY) {
            return false;
        }
        ValueVertex *transitionHClassValue = GetHeapConstant(access.transitionHClass);
        if (transitionHClassValue == nullptr) {
            return false;
        }
        if (receiverHClass->IsPrototype()) {
            return false;
        }
        HClassCheckResult checkResult = RequireKnownHClass(bcIndex, access, receiver, receiverHClass);
        if (checkResult != HClassCheckResult::SUCCESS) {
            return checkResult == HClassCheckResult::UNREACHABLE;
        }

        if (!BuildNamedStoreEagerGuards(bcIndex, access, receiver, true)) {
            return false;
        }
        ValueVertex *preparedValue = BuildCheckedNamedStoreValue(access.fieldRepresentation, value);
        if (preparedValue == nullptr) {
            return false;
        }

        self->NewVertex<TransitionHClassWithBarrierVertex>(compileInfoFacts_, currentBlock,
                                                           {glue, receiver, transitionHClassValue});

        if (access.fieldStorage == AccessFieldStorage::PROPERTIES_ARRAY) {
            auto *properties = self->NewVertex<EnsurePropertiesCapacityVertex>(
                compileInfoFacts_, currentBlock, std::initializer_list<ValueVertex *> {glue, receiver},
                static_cast<int32_t>(access.fieldIndex));
            UpdateCatchBlockData(properties);
            LoadLazyDeoptFrameStateForThrowableCall(bcIndex, properties);
            BuildPreparedNamedStoreField(properties, access.fieldOffset, preparedValue, access.fieldRepresentation);
        } else {
            BuildPreparedNamedStoreField(receiver, access.fieldOffset, preparedValue, access.fieldRepresentation);
        }
        compileInfoFacts_->RecordHClass(receiver, transitionHClass, IsHClassStableForFacts(transitionHClass, false));
        return true;
    }

    bool CanLowerNamedStoreField(const NamedStoreAccessInfo &access) const
    {
        JSHClass *receiverHClass = nullptr;
        if (access.mode != AccessMode::NAMED_STORE || !TryResolveHClassRef(access.expectedHClass, &receiverHClass)) {
            return false;
        }
        bool hasSupportedStorage = access.fieldStorage == AccessFieldStorage::IN_OBJECT ||
                                   access.fieldStorage == AccessFieldStorage::PROPERTIES_ARRAY;
        if (!hasSupportedStorage) {
            return false;
        }
        if (access.kind == AccessKind::TRANSITION) {
            JSHClass *transitionHClass = nullptr;
            bool hasSupportedRepresentation = access.fieldRepresentation == AccessFieldRepresentation::TAGGED ||
                                              access.fieldRepresentation == AccessFieldRepresentation::INT32 ||
                                              access.fieldRepresentation == AccessFieldRepresentation::DOUBLE;
            return access.holderIsReceiver && hasSupportedRepresentation && !receiverHClass->IsPrototype() &&
                   TryResolveHClassRef(access.transitionHClass, &transitionHClass);
        }
        if (access.isSharedStore) {
            return access.holderIsReceiver;
        }
        if (access.kind == AccessKind::ACCESSOR) {
            if (access.holderIsReceiver) {
                return true;
            }
            JSHClass *holderHClass = nullptr;
            return access.HasHolderHClass() && TryResolveHClassRef(access.holderHClass, &holderHClass);
        }
        if (!access.IsDataField()) {
            return false;
        }
        return access.fieldRepresentation == AccessFieldRepresentation::TAGGED ||
               access.fieldRepresentation == AccessFieldRepresentation::INT32 ||
               access.fieldRepresentation == AccessFieldRepresentation::DOUBLE;
    }

    bool TryLowerNamedStoreField(uint32_t bcIndex, const NamedStoreAccessInfo &access,
                                 ValueVertex *receiver, ValueVertex *value, PropertyKey key)
    {
        if (access.kind == AccessKind::TRANSITION) {
            return TryLowerNamedStoreTransition(bcIndex, access, receiver, value);
        }
        if (access.isSharedStore) {
            return TryLowerNamedStoreShared(bcIndex, access, receiver, value, key);
        }
        if (access.mode != AccessMode::NAMED_STORE) {
            return false;
        }

        JSHClass *receiverHClass = nullptr;
        if (!TryResolveHClassRef(access.expectedHClass, &receiverHClass)) {
            return false;
        }
        HClassCheckResult checkResult = RequireKnownHClass(bcIndex, access, receiver, receiverHClass);
        if (checkResult != HClassCheckResult::SUCCESS) {
            return checkResult == HClassCheckResult::UNREACHABLE;
        }
        if (!BuildNamedStoreEagerGuards(bcIndex, access, receiver)) {
            return false;
        }
        if (access.kind == AccessKind::ACCESSOR) {
            return TryLowerNamedStoreAccessor(bcIndex, access, receiver, value);
        }
        if (!access.IsDataField()) {
            return false;
        }

        ValueVertex *storeTarget = receiver;
        if (access.fieldStorage == AccessFieldStorage::PROPERTIES_ARRAY) {
            storeTarget = self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {receiver},
                                                                 static_cast<int32_t>(JSObject::PROPERTIES_OFFSET));
        } else if (access.fieldStorage != AccessFieldStorage::IN_OBJECT) {
            return false;
        }

        if (access.fieldRepresentation != AccessFieldRepresentation::TAGGED) {
            if (access.fieldRepresentation != AccessFieldRepresentation::INT32 &&
                access.fieldRepresentation != AccessFieldRepresentation::DOUBLE) {
                return false;
            }
            ChunkVector<ValueVertex *> storeInputs(self->chunk_);
            storeInputs.emplace_back(storeTarget);
            storeInputs.emplace_back(value);
            EagerDeoptimizableMixin *store = nullptr;
            if (access.fieldRepresentation == AccessFieldRepresentation::INT32) {
                store = NewFieldStore<StoreInt32FieldWithRepVertex>(
                    key, storeInputs, self->chunk_, access.fieldOffset,
                    self->preproc_->GetBytecodeOffset(bcIndex));
            } else {
                store = NewFieldStore<StoreDoubleFieldWithRepVertex>(
                    key, storeInputs, self->chunk_, access.fieldOffset,
                    self->preproc_->GetBytecodeOffset(bcIndex));
            }
            store->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(bcIndex));
            return true;
        }

        BuildStoreTaggedField(storeTarget, access.fieldOffset, value, key);
        if (access.holderIsReceiver) {
            compileInfoFacts_->RecordLoadedProperty(receiver, key, value);
        }
        return true;
    }

    static bool IsLocalTaggedStoreField(const NamedStoreAccessInfo &access)
    {
        return access.mode == AccessMode::NAMED_STORE && !access.isSharedStore && !access.hasProtoCell &&
               access.IsDataField() && access.holderIsReceiver &&
               access.fieldRepresentation == AccessFieldRepresentation::TAGGED &&
               (access.fieldStorage == AccessFieldStorage::IN_OBJECT ||
                access.fieldStorage == AccessFieldStorage::PROPERTIES_ARRAY) &&
               access.expectedHClass.IsSafeForCompile();
    }

    static bool IsLocalPolyNamedStoreCase(const NamedStoreAccessInfo &access)
    {
        if (access.mode != AccessMode::NAMED_STORE || access.isSharedStore ||
            !access.expectedHClass.IsSafeForCompile()) {
            return false;
        }
        if (!access.holderIsReceiver && !access.hasProtoCell) {
            return false;
        }
        if (access.fieldStorage != AccessFieldStorage::IN_OBJECT &&
            access.fieldStorage != AccessFieldStorage::PROPERTIES_ARRAY) {
            return false;
        }
        if (access.kind == AccessKind::TRANSITION) {
            bool supportedRepresentation = access.fieldRepresentation == AccessFieldRepresentation::TAGGED ||
                                           access.fieldRepresentation == AccessFieldRepresentation::INT32 ||
                                           access.fieldRepresentation == AccessFieldRepresentation::DOUBLE;
            return access.holderIsReceiver && supportedRepresentation && access.transitionHClass.IsSafeForCompile();
        }
        if (access.kind == AccessKind::ACCESSOR) {
            return access.holderIsReceiver || (access.hasFieldHClass && access.fieldHClass.IsSafeForCompile());
        }
        if (!access.IsDataField()) {
            return false;
        }
        return access.fieldRepresentation == AccessFieldRepresentation::TAGGED ||
               access.fieldRepresentation == AccessFieldRepresentation::INT32 ||
               access.fieldRepresentation == AccessFieldRepresentation::DOUBLE;
    }

    static bool HasSameStoreFieldLocation(const NamedStoreAccessInfo &left, const NamedStoreAccessInfo &right)
    {
        return left.fieldStorage == right.fieldStorage && left.fieldOffset == right.fieldOffset;
    }

    bool TryLowerEquivalentNamedStoreFields(uint32_t bcIndex, const NamedStoreAccessSet &access,
                                            ValueVertex *receiver, ValueVertex *value, PropertyKey key)
    {
        if (access.caseCount < 2 || !IsLocalTaggedStoreField(access.cases[0])) {
            return false;
        }

        std::vector<JSHClass *> expectedHClasses;
        std::vector<RequestedHClassInfo> requestedHClasses;
        expectedHClasses.reserve(access.caseCount);
        requestedHClasses.reserve(access.caseCount);
        JSHClass *firstHClass = nullptr;
        if (!TryResolveHClassRef(access.cases[0].expectedHClass, &firstHClass)) {
            return false;
        }
        expectedHClasses.push_back(firstHClass);
        requestedHClasses.push_back(RequestedHClassInfo {
            firstHClass,
            access.cases[0].expectedHClass,
            access.cases[0].dependencies.canAssumeStableHClass,
        });
        for (uint32_t i = 1; i < access.caseCount; ++i) {
            if (!IsLocalTaggedStoreField(access.cases[i]) ||
                !HasSameStoreFieldLocation(access.cases[0], access.cases[i])) {
                return false;
            }
            JSHClass *expectedHClass = nullptr;
            if (!TryResolveHClassRef(access.cases[i].expectedHClass, &expectedHClass)) {
                return false;
            }
            if (std::find(expectedHClasses.begin(), expectedHClasses.end(), expectedHClass) != expectedHClasses.end()) {
                return false;
            }
            expectedHClasses.push_back(expectedHClass);
            requestedHClasses.push_back(RequestedHClassInfo {
                expectedHClass,
                access.cases[i].expectedHClass,
                access.cases[i].dependencies.canAssumeStableHClass,
            });
        }

        HClassCheckResult checkResult = BuildHClassCheck(bcIndex, receiver, requestedHClasses, false);
        if (checkResult != HClassCheckResult::SUCCESS) {
            return checkResult == HClassCheckResult::UNREACHABLE;
        }
        ValueVertex *storeTarget = receiver;
        if (access.cases[0].fieldStorage == AccessFieldStorage::PROPERTIES_ARRAY) {
            storeTarget = self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {receiver},
                                                                 static_cast<int32_t>(JSObject::PROPERTIES_OFFSET));
        }
        BuildStoreTaggedField(storeTarget, access.cases[0].fieldOffset, value, key);
        compileInfoFacts_->RecordLoadedProperty(receiver, key, value);
        return true;
    }

    bool TryLowerPolyNamedStoreFields(uint32_t bcIndex, const NamedStoreAccessSet &access,
                                      ValueVertex *receiver, ValueVertex *value, PropertyKey key)
    {
        if (access.caseCount < 2) {
            return false;
        }

        std::vector<JSHClass *> expectedHClasses;
        NodeInfo::PossibleHClassInfos provenHClasses;
        std::vector<StoreTaggedFieldByHClassCase> storeCases;
        expectedHClasses.reserve(access.caseCount);
        provenHClasses.reserve(access.caseCount);
        storeCases.reserve(access.caseCount);
        for (uint32_t i = 0; i < access.caseCount; ++i) {
            const NamedStoreAccessInfo &storeCaseInfo = access.cases[i];
            if (!IsLocalTaggedStoreField(storeCaseInfo)) {
                return false;
            }

            JSHClass *expectedHClass = nullptr;
            if (!TryResolveHClassRef(storeCaseInfo.expectedHClass, &expectedHClass)) {
                return false;
            }
            if (std::find(expectedHClasses.begin(), expectedHClasses.end(), expectedHClass) != expectedHClasses.end()) {
                return false;
            }
            expectedHClasses.push_back(expectedHClass);
            std::optional<uint32_t> expectedHClassHandleIndex =
                GetHeapConstantHandleIndex(storeCaseInfo.expectedHClass);
            if (!expectedHClassHandleIndex.has_value()) {
                return false;
            }
            provenHClasses.push_back(NodeInfo::PossibleHClassInfo {
                .hclass = expectedHClass,
                .isStable = IsHClassStableForFacts(expectedHClass, storeCaseInfo.dependencies.canAssumeStableHClass),
            });
            storeCases.push_back(StoreTaggedFieldByHClassCase {
                expectedHClassHandleIndex.value(),
                storeCaseInfo.fieldOffset,
                storeCaseInfo.fieldStorage == AccessFieldStorage::PROPERTIES_ARRAY,
            });
        }

        ChunkVector<ValueVertex *> storeInputs(self->chunk_);
        storeInputs.emplace_back(glue);
        storeInputs.emplace_back(receiver);
        storeInputs.emplace_back(value);
        auto *store = NewFieldStore<StoreTaggedFieldByHClassVertex>(
            key, storeInputs, self->chunk_, storeCases,
            ClassifyDirectWriteBarrierValueKind(value), self->preproc_->GetBytecodeOffset(bcIndex));
        store->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(bcIndex));
        compileInfoFacts_->RecordPossibleHClasses(receiver, provenHClasses);
        compileInfoFacts_->RecordLoadedProperty(receiver, key, value);
        return true;
    }

    bool TryLowerMixedPolyNamedStores(uint32_t bcIndex, const NamedStoreAccessSet &access,
                                      ValueVertex *receiver, ValueVertex *value, PropertyKey key)
    {
        if (access.caseCount < 2) {
            return false;
        }

        std::vector<JSHClass *> expectedHClasses;
        expectedHClasses.reserve(access.caseCount);
        for (uint32_t i = 0; i < access.caseCount; ++i) {
            const NamedStoreAccessInfo &storeCase = access.cases[i];
            if (!IsLocalPolyNamedStoreCase(storeCase)) {
                return false;
            }

            JSHClass *expectedHClass = nullptr;
            if (!TryResolveHClassRef(storeCase.expectedHClass, &expectedHClass) ||
                std::find(expectedHClasses.begin(), expectedHClasses.end(), expectedHClass) != expectedHClasses.end()) {
                return false;
            }
            expectedHClasses.push_back(expectedHClass);

            if (storeCase.kind == AccessKind::TRANSITION) {
                JSHClass *transitionHClass = nullptr;
                if (expectedHClass->IsPrototype() ||
                    !TryResolveHClassRef(storeCase.transitionHClass, &transitionHClass)) {
                    return false;
                }
            }
        }

        CompileInfoFacts *entryFacts = compileInfoFacts_;
        std::vector<BB *> checkBlocks;
        std::vector<BB *> caseBlocks;
        std::vector<CompileInfoFacts *> caseExitFacts;
        checkBlocks.reserve(access.caseCount);
        caseBlocks.reserve(access.caseCount);
        caseExitFacts.reserve(access.caseCount);
        for (uint32_t i = 0; i < access.caseCount; ++i) {
            checkBlocks.push_back(self->NewBlock());
            caseBlocks.push_back(self->NewBlock());
        }
        BB *primitiveDeoptBlock = self->NewBlock();
        BB *hclassMissDeoptBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();
        primitiveDeoptBlock->SetDeferred(true);
        hclassMissDeoptBlock->SetDeferred(true);

        self->FinishBlockWithBranch<BranchIfTaggedHeapObjectVertex>(currentBlock, {receiver}, checkBlocks.front(),
                                                                    primitiveDeoptBlock);

        std::vector<uint32_t> expectedHClassHandleIndices;
        expectedHClassHandleIndices.reserve(access.caseCount);
        for (uint32_t i = 0; i < access.caseCount; ++i) {
            std::optional<uint32_t> expectedHClassHandleIndex =
                GetHeapConstantHandleIndex(access.cases[i].expectedHClass);
            if (!expectedHClassHandleIndex.has_value()) {
                return false;
            }
            expectedHClassHandleIndices.push_back(expectedHClassHandleIndex.value());
        }

        ValueVertex *actualHClass = nullptr;
        for (uint32_t i = 0; i < access.caseCount; ++i) {
            currentBlock = checkBlocks[i];
            compileInfoFacts_ = entryFacts;
            if (actualHClass == nullptr) {
                actualHClass = self->NewVertex<LoadHClassAddressVertex>(
                    currentBlock, std::initializer_list<ValueVertex *> {receiver});
            }
            BB *nextBlock = i + 1 < access.caseCount ? checkBlocks[i + 1] : hclassMissDeoptBlock;
            self->FinishBlockWithBranch<BranchIfHClassInVertex>(
                currentBlock, {actualHClass}, caseBlocks[i], nextBlock, self->chunk_,
                std::vector<uint32_t> {expectedHClassHandleIndices[i]}, true);

            currentBlock = caseBlocks[i];
            compileInfoFacts_ = entryFacts->Clone();
            compileInfoFacts_->RecordHClass(
                receiver, expectedHClasses[i],
                IsHClassStableForFacts(expectedHClasses[i],
                                       access.cases[i].dependencies.canAssumeStableHClass));
            bool lowered = TryLowerNamedStoreField(bcIndex, access.cases[i], receiver, value, key);
            ASSERT(lowered);
            if (!lowered) {
                UNREACHABLE();
            }
            caseExitFacts.push_back(compileInfoFacts_);
            self->FinishBlockWithJump(currentBlock, doneBlock);
        }

        auto buildDeoptBlock = [&](BB *deoptBlock) {
            currentBlock = deoptBlock;
            compileInfoFacts_ = entryFacts->Clone();
            auto *deopt =
                self->FinishBlockWith<DeoptVertex>(currentBlock, {}, self->chunk_, kungfu::DeoptType::KEYMISSMATCH,
                                                   self->preproc_->GetBytecodeOffset(bcIndex));
            deopt->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(bcIndex));
        };
        buildDeoptBlock(primitiveDeoptBlock);
        buildDeoptBlock(hclassMissDeoptBlock);

        currentBlock = doneBlock;
        ASSERT(!caseExitFacts.empty());
        compileInfoFacts_ = caseExitFacts.front()->Clone();
        for (size_t i = 1; i < caseExitFacts.size(); ++i) {
            compileInfoFacts_->Merge(*caseExitFacts[i]);
        }
        return true;
    }

    bool TryLowerNamedStoreAccessSet(uint32_t bcIndex, const NamedStoreAccessSet &access,
                                     ValueVertex *receiver, ValueVertex *value, PropertyKey key)
    {
        if (access.caseCount == 0) {
            return false;
        }
        if (access.caseCount == 1) {
            JSHClass *expectedHClass = nullptr;
            if (!TryResolveHClassRef(access.cases[0].expectedHClass, &expectedHClass)) {
                return false;
            }
            HClassCheckResult checkResult =
                BuildCheckSingleHClass(bcIndex, receiver, expectedHClass, access.cases[0].expectedHClass, false,
                                       access.cases[0].dependencies.canAssumeStableHClass);
            if (checkResult != HClassCheckResult::SUCCESS) {
                return checkResult == HClassCheckResult::UNREACHABLE;
            }
            return TryLowerNamedStoreField(bcIndex, access.cases[0], receiver, value, key);
        }

        JSHClass *knownHClass = TryGetFreshKnownHClass(receiver);
        HClassInference inference(this, receiver, HClassInference::Mode::ALLOW_STALE);
        std::optional<NodeInfo::PossibleHClasses> inferredHClasses = inference.TryGetPossibleHClasses();
        if (knownHClass == nullptr && inferredHClasses.has_value() && inferredHClasses->size() == 1) {
            knownHClass = inferredHClasses->front();
        }
        if (knownHClass != nullptr) {
            const NamedStoreAccessInfo *matched = nullptr;
            bool ambiguousMatch = false;
            for (uint32_t i = 0; i < access.caseCount; ++i) {
                JSHClass *caseHClass = nullptr;
                if (!TryResolveHClassRef(access.cases[i].expectedHClass, &caseHClass)) {
                    return false;
                }
                if (caseHClass != knownHClass) {
                    continue;
                }
                if (matched != nullptr) {
                    matched = nullptr;
                    ambiguousMatch = true;
                    break;
                }
                matched = &access.cases[i];
            }
            if (!ambiguousMatch && matched != nullptr && CanLowerNamedStoreField(*matched)) {
                HClassCheckResult freshnessResult = inference.EnsureHClassesFresh(bcIndex);
                if (freshnessResult == HClassCheckResult::UNREACHABLE) {
                    return true;
                }
                if (freshnessResult == HClassCheckResult::SUCCESS) {
                    return TryLowerNamedStoreField(bcIndex, *matched, receiver, value, key);
                }
            }
        }

        return TryLowerEquivalentNamedStoreFields(bcIndex, access, receiver, value, key) ||
            TryLowerPolyNamedStoreFields(bcIndex, access, receiver, value, key) ||
            TryLowerMixedPolyNamedStores(bcIndex, access, receiver, value, key);
    }

    void BuildTypedArrayLengthGuards(uint32_t bcIndex, ValueVertex *object, const NamedLoadAccessInfo &accessInfo)
    {
        // Replacing an accessor in place need not change its holder's HClass.
        auto *guard = self->NewVertex<DeoptIfPrototypeChangedVertex>(
            currentBlock, {object}, self->chunk_, true, false, self->preproc_->GetBytecodeOffset(bcIndex));
        guard->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(bcIndex));
        BuildDeoptIfArrayBufferDetached(object, accessInfo.typedArrayOnHeapMode);
    }

    ValueVertex *TryBuildPropertyLoad(uint32_t bcIndex, ValueVertex *object, const LoadedPropertyKey &key,
                                      const NamedLoadAccessInfo &accessInfo)
    {
        if (accessInfo.isJSArrayLength) {
            return self->NewVertex<U32ToTaggedNumberVertex>(
                compileInfoFacts_, currentBlock, {BuildLoadJSArrayLength(object)});
        }
        if (accessInfo.typedArrayLengthType != JSType::INVALID) {
            BuildTypedArrayLengthGuards(bcIndex, object, accessInfo);
            return self->NewVertex<U32ToTaggedNumberVertex>(compileInfoFacts_, currentBlock,
                {BuildLoadTypedArrayLength(object, accessInfo.typedArrayLengthType)});
        }
        ValueVertex *cached = compileInfoFacts_->LookupLoadedProperty(key);
        if (cached == nullptr) {
            cached = compileInfoFacts_->LookupLoadedConstantProperty(key);
        }
        if (cached != nullptr) {
            return cached;
        }

        ValueVertex *loadSource = BuildPropertyLoadSource(bcIndex, object, accessInfo);
        if (loadSource == nullptr) {
            return nullptr;
        }
        ValueVertex *result = BuildLoadField(loadSource, accessInfo.plr);
        if (accessInfo.isConst) {
            compileInfoFacts_->RecordLoadedConstantProperty(key, result);
        } else {
            compileInfoFacts_->RecordLoadedProperty(key, result);
        }
        return result;
    }

    ValueVertex *BuildPolymorphicPropertyLoad(uint32_t bcIndex, ValueVertex *object,
                                              const NamedLoadAccessInfo &accessInfo)
    {
        // Internal case blocks share CompileInfoFacts. Do not let a load from one sibling case
        // enter the property cache or available-expression table and leak into another case.
        if (accessInfo.isJSArrayLength) {
            ValueVertex *length = self->NewVertex<LoadInt32FieldVertex>(
                currentBlock, {object}, static_cast<int32_t>(JSArray::LENGTH_OFFSET));
            return self->NewVertex<U32ToTaggedNumberVertex>(currentBlock, {length});
        }
        if (accessInfo.typedArrayLengthType != JSType::INVALID) {
            BuildTypedArrayLengthGuards(bcIndex, object, accessInfo);
            ValueVertex *length = self->NewVertex<LoadInt32FieldVertex>(
                currentBlock, {object}, static_cast<int32_t>(JSTypedArray::ARRAY_LENGTH_OFFSET));
            return self->NewVertex<U32ToTaggedNumberVertex>(currentBlock, {length});
        }
        ValueVertex *loadSource = BuildPropertyLoadSource(bcIndex, object, accessInfo);
        return BuildLoadFieldWithoutCse(loadSource, accessInfo.plr);
    }

    bool TryBuildPolymorphicNamedAccess(uint32_t bcIndex, ValueVertex *receiver,
                                        const std::vector<NamedLoadAccessInfo> &accessInfos)
    {
        ASSERT(accessInfos.size() > 1);
        std::vector<JSHClass *> allExpectedHClasses;
        std::vector<std::vector<uint32_t>> expectedHClassHandleIndices;
        expectedHClassHandleIndices.reserve(accessInfos.size());
        for (const NamedLoadAccessInfo &accessInfo : accessInfos) {
            if (accessInfo.lookupStartObjectHClasses.empty() ||
                accessInfo.lookupStartObjectHClasses.size() != accessInfo.lookupStartObjectHClassRefs.size()) {
                return false;
            }
            for (uint32_t i = 0; i < accessInfo.lookupStartObjectHClasses.size(); ++i) {
                JSHClass *hclass = accessInfo.lookupStartObjectHClasses[i];
                if (hclass == nullptr || hclass->IsString() ||
                    std::find(allExpectedHClasses.begin(), allExpectedHClasses.end(), hclass) !=
                        allExpectedHClasses.end()) {
                    return false;
                }
                allExpectedHClasses.push_back(hclass);
            }
            std::vector<uint32_t> groupHandleIndices;
            if (!GetHeapConstantHandleIndices(accessInfo.lookupStartObjectHClassRefs, &groupHandleIndices)) {
                return false;
            }
            expectedHClassHandleIndices.push_back(std::move(groupHandleIndices));
            if (accessInfo.holderDepth != 0 && !accessInfo.hasStableProtoChain) {
                std::vector<uint32_t> prototypeHandleIndices;
                if (!GetHeapConstantHandleIndices(accessInfo.expectedPrototypeHClassRefs, &prototypeHandleIndices)) {
                    return false;
                }
            }
        }

        CompileInfoFacts *entryFacts = compileInfoFacts_;
        std::vector<BB *> checkBlocks;
        std::vector<BB *> caseBlocks;
        checkBlocks.reserve(accessInfos.size());
        caseBlocks.reserve(accessInfos.size());
        for (uint32_t i = 0; i < accessInfos.size(); ++i) {
            checkBlocks.push_back(self->NewBlock());
            caseBlocks.push_back(self->NewBlock());
        }
        BB *primitiveDeoptBlock = self->NewBlock();
        BB *hclassMissDeoptBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();
        primitiveDeoptBlock->SetDeferred(true);
        hclassMissDeoptBlock->SetDeferred(true);

        self->FinishBlockWithBranch<BranchIfTaggedHeapObjectVertex>(currentBlock, {receiver}, checkBlocks.front(),
                                                                    primitiveDeoptBlock);

        ValueVertex *actualHClass = nullptr;
        for (uint32_t i = 0; i < accessInfos.size(); ++i) {
            currentBlock = checkBlocks[i];
            compileInfoFacts_ = entryFacts;
            if (actualHClass == nullptr) {
                actualHClass = self->NewVertex<LoadHClassAddressVertex>(
                    compileInfoFacts_, currentBlock, std::initializer_list<ValueVertex *> {receiver});
            }
            BB *nextBlock = i + 1 < accessInfos.size() ? checkBlocks[i + 1] : hclassMissDeoptBlock;
            self->FinishBlockWithBranch<BranchIfHClassInVertex>(currentBlock, {actualHClass}, caseBlocks[i], nextBlock,
                                                                self->chunk_, expectedHClassHandleIndices[i], true);
        }

        std::vector<ValueVertex *> results;
        results.reserve(accessInfos.size());
        for (uint32_t i = 0; i < accessInfos.size(); ++i) {
            currentBlock = caseBlocks[i];
            compileInfoFacts_ = entryFacts;
            ValueVertex *result = BuildPolymorphicPropertyLoad(bcIndex, receiver, accessInfos[i]);
            ASSERT(result != nullptr);
            if (result == nullptr) {
                UNREACHABLE();
            }
            results.push_back(result);
            self->FinishBlockWithJump(currentBlock, doneBlock);
        }

        auto buildDeoptBlock = [&](BB *deoptBlock) {
            currentBlock = deoptBlock;
            compileInfoFacts_ = entryFacts->Clone();
            auto *deopt =
                self->FinishBlockWith<DeoptVertex>(currentBlock, {}, self->chunk_, kungfu::DeoptType::KEYMISSMATCH,
                                                   self->preproc_->GetBytecodeOffset(bcIndex));
            deopt->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(bcIndex));
        };
        buildDeoptBlock(primitiveDeoptBlock);
        buildDeoptBlock(hclassMissDeoptBlock);

        currentBlock = doneBlock;
        compileInfoFacts_ = entryFacts;
        if (!allExpectedHClasses.empty()) {
            compileInfoFacts_->RecordPossibleHClasses(receiver, allExpectedHClasses, false);
        }
        frameState.SetAcc(self->NewPhiVertexWith(currentBlock, results, self->AccIndex()));
        return true;
    }

    bool TryBuildNamedAccess(uint32_t bcIndex, ValueVertex *receiver, uint16_t constDataId,
                             const std::vector<NamedLoadAccessInfo> &accessInfos)
    {
        if (accessInfos.empty()) {
            return false;
        }
        if (accessInfos.size() > 1) {
            return TryBuildPolymorphicNamedAccess(bcIndex, receiver, accessInfos);
        }
        const NamedLoadAccessInfo &accessInfo = accessInfos.front();
        const std::vector<JSHClass *> &maps = accessInfo.lookupStartObjectHClasses;
        bool hasHClassOfString = std::any_of(maps.begin(), maps.end(),
                                             [](JSHClass *hclass) { return hclass != nullptr && hclass->IsString(); });
        if (hasHClassOfString) {
            return false;
        }
        HClassCheckResult checkResult = BuildCheckAnyOfHClasses(
            bcIndex, receiver, maps, accessInfo.lookupStartObjectHClassRefs, false, accessInfo.canAssumeStableHClasses);
        if (checkResult != HClassCheckResult::SUCCESS) {
            return checkResult == HClassCheckResult::UNREACHABLE;
        }
        LoadedPropertyKey propertyKey = LoadedPropertyKey::ConstDataId(receiver, constDataId, accessInfo.plr);
        ValueVertex *result = TryBuildPropertyLoad(bcIndex, receiver, propertyKey, accessInfo);
        if (result == nullptr) {
            return false;
        }
        frameState.SetAcc(result);
        return true;
    }

    bool TryBuildLoadNamedProperty(const BytecodeInfo *bcInfo, uint32_t bcIndex, ValueVertex *receiver,
                                   uint16_t constDataId)
    {
        // Successful own-field stores cache the JS value independently of the field's physical layout.
        if (ValueVertex *stored = compileInfoFacts_->LookupLoadedProperty(
                receiver, PropertyKey::ConstDataId(constDataId))) {
            frameState.SetAcc(stored);
            return true;
        }
        auto factory = self->pgoContext_.CreateAccessInfoFactory(bcInfo);
        PropertyAccessSet accessSet;
        if (!factory.TryBuildNamedLoadAccessInfo(0, &accessSet)) {
            return false;
        }

        NamedLoadAccessInfosOpt accessInfos = TryGetLoadObjByNameAccessInfos(accessSet, constDataId);
        if (!accessInfos.has_value()) {
            return false;
        }
        if (!TryBuildNamedAccess(bcIndex, receiver, constDataId, accessInfos.value())) {
            return false;
        }
        return true;
    }

    ValueVertex *LoadValueFeedbackKey(uint32_t slotId)
    {
        ValueVertex *function = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *profileCell = self->NewVertex<LoadTaggedFieldVertex>(
            currentBlock, {function}, static_cast<int32_t>(JSFunction::RAW_PROFILE_TYPE_INFO_OFFSET));
        ValueVertex *profile = self->NewVertex<LoadTaggedFieldVertex>(
            currentBlock, {profileCell}, static_cast<int32_t>(ProfileTypeInfoCell::VALUE_OFFSET));
        int32_t slotOffset =
            static_cast<int32_t>(ProfileTypeInfo::DATA_OFFSET + slotId * JSTaggedValue::TaggedTypeSize());
        return self->NewVertex<LoadTaggedFieldVertex>(currentBlock, {profile}, slotOffset);
    }

    void BuildCheckValueKey(uint32_t bcIndex, ValueVertex *key, uint32_t slotId)
    {
        ValueVertex *cachedKey = LoadValueFeedbackKey(slotId);
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(bcIndex);
        auto *check = self->NewVertex<DeoptIfTaggedConditionVertex>(
            currentBlock, {key, cachedKey}, self->chunk_, self->preproc_->GetBytecodeOffset(bcIndex),
            Condition::NOT_EQUAL, kungfu::DeoptType::KEYMISSMATCH);
        check->SetEagerDeoptFrameState(std::move(deoptFrameState));
    }

    bool TryBuildNamedAccessByValue(uint32_t bcIndex, ValueVertex *receiver, uint32_t propertyKeyHandleIndex,
                                    const std::vector<NamedLoadAccessInfo> &accessInfos)
    {
        if (accessInfos.empty()) {
            return false;
        }
        if (accessInfos.size() > 1) {
            return TryBuildPolymorphicNamedAccess(bcIndex, receiver, accessInfos);
        }

        const NamedLoadAccessInfo &accessInfo = accessInfos.front();
        const std::vector<JSHClass *> &hclasses = accessInfo.lookupStartObjectHClasses;
        bool hasStringHClass = std::any_of(hclasses.begin(), hclasses.end(),
                                           [](JSHClass *hclass) { return hclass != nullptr && hclass->IsString(); });
        if (hasStringHClass) {
            return false;
        }
        HClassCheckResult checkResult =
            BuildCheckAnyOfHClasses(bcIndex, receiver, hclasses, accessInfo.lookupStartObjectHClassRefs, false,
                                    accessInfo.canAssumeStableHClasses);
        if (checkResult != HClassCheckResult::SUCCESS) {
            return checkResult == HClassCheckResult::UNREACHABLE;
        }

        LoadedPropertyKey key = LoadedPropertyKey::HeapConstant(receiver, propertyKeyHandleIndex, accessInfo.plr);
        ValueVertex *result = TryBuildPropertyLoad(bcIndex, receiver, key, accessInfo);
        if (result == nullptr) {
            return false;
        }
        frameState.SetAcc(result);
        return true;
    }

    void BuildCheckTaggedCondition(uint32_t bcIndex, ValueVertex *left, ValueVertex *right, Condition condition,
                                   kungfu::DeoptType deoptType)
    {
        EagerDeoptFrameState deoptFrameState = BuildCurrentEagerDeoptFrameState(bcIndex);
        auto *check = self->NewVertex<DeoptIfTaggedConditionVertex>(currentBlock, {left, right}, self->chunk_,
                                                                    self->preproc_->GetBytecodeOffset(bcIndex),
                                                                    condition, deoptType);
        check->SetEagerDeoptFrameState(std::move(deoptFrameState));
    }

    bool TryResolveNormalElementLoadHClass(const ElementLoadAccessInfo &accessInfo, JSHClass **receiverHClass)
    {
        if (accessInfo.kind != ElementLoadKind::NORMAL || HandlerBase::NeedSkipInPGODump(accessInfo.handlerInfo)) {
            return false;
        }

        if (!TryResolveHClassRef(accessInfo.expectedHClass, receiverHClass) || *receiverHClass == nullptr ||
            ((*receiverHClass)->GetObjectType() != JSType::JS_OBJECT && !(*receiverHClass)->IsJSArray()) ||
            (*receiverHClass)->IsDictionaryElement() ||
            HandlerBase::IsJSArray(accessInfo.handlerInfo) != (*receiverHClass)->IsJSArray()) {
            return false;
        }
        bool isMutantArrayEnabled = self->preproc_->GetEnv()->GetJSOptions().IsEnableMutantArray();
        if (isMutantArrayEnabled) {
            ElementsKind kind = (*receiverHClass)->GetElementsKind();
            if (Elements::IsIntOrHoleInt(kind) || Elements::IsNumberOrHoleNumber(kind)) {
                return false;
            }
        }
        return true;
    }

    bool AppendCompatibleNormalElementHClasses(JSHClass *receiverHClass, const ArkSteedHClassRef &receiverHClassRef,
                                               bool isMutantArrayEnabled, std::vector<JSHClass *> *receiverHClasses,
                                               std::vector<ArkSteedHClassRef> *receiverHClassRefs)
    {
        ASSERT(receiverHClasses != nullptr && receiverHClassRefs != nullptr);
        ASSERT(receiverHClasses->size() == receiverHClassRefs->size());
        if (receiverHClass == nullptr || !receiverHClassRef.IsSafeForCompile()) {
            return false;
        }
        if (std::find(receiverHClasses->begin(), receiverHClasses->end(), receiverHClass) == receiverHClasses->end()) {
            receiverHClasses->push_back(receiverHClass);
            receiverHClassRefs->push_back(receiverHClassRef);
        }

        // Large array literals currently use a specialized initial HClass in the interpreter but the generic
        // initial HClass after their creator tiers up. When mutant arrays are disabled, both layouts store tagged
        // elements, so accepting the generic companion avoids repeated deopts without weakening the element load.
        if (!isMutantArrayEnabled) {
            ArkSteedHClassRef genericHClassRef;
            JSHClass *genericHClass = TryGetGenericInitialArrayHClass(receiverHClass, &genericHClassRef);
            if (genericHClass != nullptr && std::find(receiverHClasses->begin(), receiverHClasses->end(),
                                                      genericHClass) == receiverHClasses->end()) {
                receiverHClasses->push_back(genericHClass);
                receiverHClassRefs->push_back(genericHClassRef);
            }
        }
        return true;
    }

    void BuildNormalElementLoad(uint32_t bcIndex, ValueVertex *receiver, ValueVertex *key)
    {
        ValueVertex *index = BuildCheckedTaggedIntToI32(key);
        ValueVertex *elements = BuildLoadElements(receiver);
        ValueVertex *capacity = self->NewVertex<LoadInt32FieldVertex>(
            compileInfoFacts_, currentBlock, {elements}, static_cast<int32_t>(TaggedArray::LENGTH_OFFSET));
        BuildDeoptIfInt32Condition(
            index, capacity, Condition::ABOVE_OR_EQUAL, kungfu::DeoptType::RANGE_ERROR);

        ValueVertex *result =
            self->NewVertex<LoadTaggedElementVertex>(compileInfoFacts_, currentBlock, {elements, index});
        ValueVertex *hole = self->graph_->GetTaggedConstant(JSTaggedValue::VALUE_HOLE);
        BuildCheckTaggedCondition(bcIndex, result, hole, Condition::EQUAL, kungfu::DeoptType::BUILTINISHOLE1);
        frameState.SetAcc(result);
    }

    bool TryBuildNormalElementLoad(uint32_t bcIndex, ValueVertex *receiver, ValueVertex *key,
                                   const ElementLoadAccessInfo &accessInfo)
    {
        JSHClass *receiverHClass = nullptr;
        if (!TryResolveNormalElementLoadHClass(accessInfo, &receiverHClass)) {
            return false;
        }

        bool isMutantArrayEnabled = self->preproc_->GetEnv()->GetJSOptions().IsEnableMutantArray();
        std::vector<JSHClass *> receiverHClasses;
        std::vector<ArkSteedHClassRef> receiverHClassRefs;
        if (!AppendCompatibleNormalElementHClasses(receiverHClass, accessInfo.expectedHClass, isMutantArrayEnabled,
                                                   &receiverHClasses, &receiverHClassRefs)) {
            return false;
        }
        HClassCheckResult checkResult =
            receiverHClasses.size() == 1
                ? BuildCheckSingleHClass(bcIndex, receiver, receiverHClass, accessInfo.expectedHClass, true, false)
                : BuildCheckAnyOfHClasses(bcIndex, receiver, receiverHClasses, receiverHClassRefs, false, false);
        if (checkResult != HClassCheckResult::SUCCESS) {
            return checkResult == HClassCheckResult::UNREACHABLE;
        }

        BuildNormalElementLoad(bcIndex, receiver, key);
        return true;
    }

    bool TryBuildStringElementLoad(uint32_t bcIndex, ValueVertex *receiver, ValueVertex *key,
                                   const ElementLoadAccessInfo &accessInfo)
    {
        if (accessInfo.kind != ElementLoadKind::STRING || HandlerBase::NeedSkipInPGODump(accessInfo.handlerInfo)) {
            return false;
        }

        JSHClass *receiverHClass = nullptr;
        if (!TryResolveHClassRef(accessInfo.expectedHClass, &receiverHClass) || receiverHClass == nullptr ||
            !receiverHClass->IsLineString()) {
            return false;
        }
        HClassCheckResult checkResult =
            BuildCheckSingleHClass(bcIndex, receiver, receiverHClass, accessInfo.expectedHClass, true, false);
        if (checkResult != HClassCheckResult::SUCCESS) {
            return checkResult == HClassCheckResult::UNREACHABLE;
        }

        ValueVertex *index = BuildCheckedTaggedIntToI32(key);
        ValueVertex *lengthAndFlags = self->NewVertex<LoadInt32FieldVertex>(
            compileInfoFacts_, currentBlock, {receiver}, static_cast<int32_t>(BaseString::LENGTH_AND_FLAGS_OFFSET));
        ValueVertex *lengthShift =
            self->graph_->GetInt32Constant(static_cast<int32_t>(BaseString::LengthBits::START_BIT));
        ValueVertex *length = self->NewVertex<I32BitwiseBinaryVertex>(currentBlock, {lengthAndFlags, lengthShift},
                                                                      IntBitwiseKind::SHIFT_RIGHT_LOGICAL);
        BuildDeoptIfInt32Condition(index, length, Condition::ABOVE_OR_EQUAL, kungfu::DeoptType::RANGE_ERROR);

        ValueVertex *charCode = self->NewVertex<LineStringLoadElementVertex>(compileInfoFacts_, currentBlock,
                                                                             {receiver, index, lengthAndFlags});

        // SingleCharTable caches one-character strings for codes [1, 0x7f].
        ValueVertex *one = self->graph_->GetInt32Constant(LoadSingleCharTableElementVertex::MIN_CHAR_CODE);
        ValueVertex *cachedRange = self->graph_->GetInt32Constant(LoadSingleCharTableElementVertex::MAX_CHAR_CODE -
                                                                  LoadSingleCharTableElementVertex::MIN_CHAR_CODE);
        ValueVertex *adjustedCharCode = self->NewVertex<I32SubVertex>(compileInfoFacts_, currentBlock, {charCode, one});

        CompileInfoFacts *entryFacts = compileInfoFacts_;
        BB *cachedCharBlock = self->NewBlock();
        BB *createCharBlock = self->NewBlock();
        BB *doneBlock = self->NewBlock();
        createCharBlock->SetDeferred(true);
        self->FinishBlockWithBranch<BranchIfInt32CompareVertex>(
            currentBlock, {adjustedCharCode, cachedRange}, cachedCharBlock, createCharBlock, Condition::BELOW_OR_EQUAL);

        currentBlock = cachedCharBlock;
        compileInfoFacts_ = entryFacts->Clone();
        ValueVertex *cachedChar =
            self->NewVertex<LoadSingleCharTableElementVertex>(compileInfoFacts_, currentBlock, {glue, charCode});
        self->FinishBlockWithJump(currentBlock, doneBlock);
        CompileInfoFacts *cachedCharFacts = compileInfoFacts_;

        currentBlock = createCharBlock;
        compileInfoFacts_ = entryFacts->Clone();
        ValueVertex *createdChar =
            CommonStubCallToAccWithLazyDeopt(
            {glue, charCode, GlobalEnv()}, CommonStubID::CreateStringBySingleCharCode, SideEffectKind::SAFE_CALL);
        self->FinishBlockWithJump(currentBlock, doneBlock);

        currentBlock = doneBlock;
        compileInfoFacts_->Merge(*cachedCharFacts);
        frameState.SetAcc(self->NewPhiVertexWith(currentBlock, {cachedChar, createdChar}, self->AccIndex()));
        return true;
    }

    bool TryFoldConstantStringElement(ValueVertex *receiver, ValueVertex *key)
    {
        std::optional<int32_t> index = TryGetInt32Value(key);
        if (!index.has_value() || *index < 0) {
            return false;
        }

        std::optional<ArkSteedHeapRef> stringRef = TryGetConstantHeapRef(receiver);
        if (!stringRef.has_value()) {
            auto *loadString = receiver->TryCast<CallCommonStubVertex>();
            if (loadString == nullptr ||
                loadString->GetCommonStubID() != static_cast<uint32_t>(CommonStubID::GetStringFromConstPool)) {
                return false;
            }
            auto *stringId = loadString->GetInput(2)->TryCast<Int32ConstantVertex>();
            if (stringId == nullptr || stringId->GetValue() < 0 ||
                stringId->GetValue() > std::numeric_limits<uint16_t>::max()) {
                return false;
            }
            stringRef = TryGetNameRefFromConstDataId(static_cast<uint16_t>(stringId->GetValue()));
        }
        JitCompilationEnv *env = self->preproc_->GetEnv();
        JSThread *thread = env == nullptr ? nullptr : env->GetJSThread();
        ArkSteedHeapBroker *broker = self->pgoContext_.GetBroker();
        if (!stringRef.has_value() || thread == nullptr || broker == nullptr) {
            return false;
        }

        uint16_t charCode = 0;
        if (!broker->TryGetStringChar(*stringRef, static_cast<uint32_t>(*index), &charCode)) {
            return false;
        }
        if (charCode < LoadSingleCharTableElementVertex::MIN_CHAR_CODE ||
            charCode > LoadSingleCharTableElementVertex::MAX_CHAR_CODE) {
            return false;
        }

        ValueVertex *charCodeConstant = self->graph_->GetInt32Constant(static_cast<int32_t>(charCode));
        frameState.SetAcc(self->NewVertex<LoadSingleCharTableElementVertex>(compileInfoFacts_, currentBlock,
                                                                            {glue, charCodeConstant}));
        return true;
    }

    bool TryResolveTypedArrayElementLoadHClass(const ElementLoadAccessInfo &accessInfo, JSHClass **receiverHClass)
    {
        if (accessInfo.kind != ElementLoadKind::TYPED_ARRAY || HandlerBase::NeedSkipInPGODump(accessInfo.handlerInfo)) {
            return false;
        }

        if (!TryResolveHClassRef(accessInfo.expectedHClass, receiverHClass) || *receiverHClass == nullptr ||
            !(*receiverHClass)->IsTypedArray()) {
            return false;
        }
        JSType objectType = (*receiverHClass)->GetObjectType();
        if (objectType <= JSType::JS_TYPED_ARRAY_FIRST || objectType > JSType::JS_FLOAT64_ARRAY ||
            HandlerBase::IsOnHeap(accessInfo.handlerInfo) != (*receiverHClass)->IsOnHeapFromBitField()) {
            return false;
        }
        return true;
    }

    bool TryBuildTypedArrayElementLoad(uint32_t bcIndex, ValueVertex *receiver, ValueVertex *key,
                                       const ElementLoadAccessInfo &accessInfo, ValueVertex *checkedIndex = nullptr,
                                       bool buildHClassCheck = true)
    {
        JSHClass *receiverHClass = nullptr;
        if (!TryResolveTypedArrayElementLoadHClass(accessInfo, &receiverHClass)) {
            return false;
        }
        if (buildHClassCheck) {
            HClassCheckResult checkResult =
                BuildCheckSingleHClass(bcIndex, receiver, receiverHClass, accessInfo.expectedHClass, true, false);
            if (checkResult != HClassCheckResult::SUCCESS) {
                return checkResult == HClassCheckResult::UNREACHABLE;
            }
        }
        JSType objectType = receiverHClass->GetObjectType();

        ValueVertex *index = checkedIndex == nullptr ? BuildCheckedTaggedIntToI32(key) : checkedIndex;
        bool isOnHeap = receiverHClass->IsOnHeapFromBitField();
        ValueVertex *storage =
            self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {receiver},
                                                   static_cast<int32_t>(JSTypedArray::VIEWED_ARRAY_BUFFER_OFFSET));
        if (!isOnHeap) {
            storage = self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {storage},
                                                             static_cast<int32_t>(JSArrayBuffer::DATA_OFFSET));
            ValueVertex *null = self->graph_->GetTaggedConstant(JSTaggedValue::VALUE_NULL);
            BuildCheckTaggedCondition(bcIndex, storage, null, Condition::EQUAL,
                                      kungfu::DeoptType::ARRAYBUFFERISDETACHED);
        }

        ValueVertex *length = BuildLoadTypedArrayLength(receiver, objectType);
        BuildDeoptIfInt32Condition(
            index, length, Condition::ABOVE_OR_EQUAL, kungfu::DeoptType::RANGE_ERROR);

        switch (objectType) {
            case JSType::JS_INT8_ARRAY:
            case JSType::JS_UINT8_ARRAY:
            case JSType::JS_UINT8_CLAMPED_ARRAY:
            case JSType::JS_INT16_ARRAY:
            case JSType::JS_UINT16_ARRAY:
            case JSType::JS_INT32_ARRAY: {
                ValueVertex *raw = self->NewVertex<TypedArrayIntLoadElementVertex>(
                    compileInfoFacts_, currentBlock, {receiver, index, storage}, objectType, isOnHeap);
                frameState.SetAcc(BuildTaggedI32Result(raw));
                break;
            }
            case JSType::JS_UINT32_ARRAY:
            case JSType::JS_FLOAT32_ARRAY:
            case JSType::JS_FLOAT64_ARRAY: {
                ValueVertex *raw = self->NewVertex<TypedArrayDoubleLoadElementVertex>(
                    compileInfoFacts_, currentBlock, {receiver, index, storage}, objectType, isOnHeap);
                frameState.SetAcc(BuildTaggedF64Value(raw));
                break;
            }
            default:
                UNREACHABLE();
        }
        return true;
    }

    bool TryBuildPolymorphicTypedArrayElementLoad(uint32_t bcIndex, ValueVertex *receiver, ValueVertex *key,
                                                  const ValueLoadAccessSet &access)
    {
        if (access.elementCount < 2) {
            return false;
        }

        std::vector<JSHClass *> expectedHClasses;
        std::vector<ArkSteedHClassRef> expectedHClassRefs;
        expectedHClasses.reserve(access.elementCount);
        expectedHClassRefs.reserve(access.elementCount);
        for (uint32_t i = 0; i < access.elementCount; ++i) {
            JSHClass *hclass = nullptr;
            if (!TryResolveTypedArrayElementLoadHClass(access.elements[i], &hclass) ||
                std::find(expectedHClasses.begin(), expectedHClasses.end(), hclass) != expectedHClasses.end()) {
                return false;
            }
            expectedHClasses.push_back(hclass);
            expectedHClassRefs.push_back(access.elements[i].expectedHClass);
        }
        HClassCheckResult checkResult =
            BuildCheckAnyOfHClasses(bcIndex, receiver, expectedHClasses, expectedHClassRefs, false, false);
        if (checkResult != HClassCheckResult::SUCCESS) {
            return checkResult == HClassCheckResult::UNREACHABLE;
        }

        ValueVertex *index = BuildCheckedTaggedIntToI32(key);
        BB *doneBlock = self->NewBlock();
        CompileInfoFacts *entryFacts = compileInfoFacts_;
        ValueVertex *entryAcc = frameState.GetAcc();
        std::vector<CompileInfoFacts *> caseExitFacts;
        caseExitFacts.reserve(access.elementCount);
        std::vector<ValueVertex *> results;
        results.reserve(access.elementCount);
        for (uint32_t i = 0; i + 1 < access.elementCount; ++i) {
            BB *caseBlock = self->NewBlock();
            BB *nextCaseBlock = self->NewBlock();
            std::optional<uint32_t> expectedHClassHandleIndex = GetHeapConstantHandleIndex(expectedHClassRefs[i]);
            if (!expectedHClassHandleIndex.has_value()) {
                return false;
            }
            self->FinishBlockWithBranch<BranchIfHClassInVertex>(
                currentBlock, {receiver}, caseBlock, nextCaseBlock, self->chunk_,
                std::vector<uint32_t> {expectedHClassHandleIndex.value()});

            currentBlock = caseBlock;
            compileInfoFacts_ = entryFacts->Clone();
            frameState.SetAcc(entryAcc);
            if (!TryBuildTypedArrayElementLoad(bcIndex, receiver, key, access.elements[i], index, false)) {
                return false;
            }
            results.push_back(frameState.GetAcc());
            caseExitFacts.push_back(compileInfoFacts_);
            self->FinishBlockWithJump(currentBlock, doneBlock);
            currentBlock = nextCaseBlock;
            compileInfoFacts_ = entryFacts;
        }

        compileInfoFacts_ = entryFacts->Clone();
        frameState.SetAcc(entryAcc);
        if (!TryBuildTypedArrayElementLoad(bcIndex, receiver, key, access.elements[access.elementCount - 1], index,
                                           false)) {
            return false;
        }
        results.push_back(frameState.GetAcc());
        caseExitFacts.push_back(compileInfoFacts_);
        self->FinishBlockWithJump(currentBlock, doneBlock);
        currentBlock = doneBlock;
        compileInfoFacts_ = caseExitFacts.front()->Clone();
        for (size_t i = 1; i < caseExitFacts.size(); ++i) {
            compileInfoFacts_->Merge(*caseExitFacts[i]);
        }
        frameState.SetAcc(self->NewPhiVertexWith(currentBlock, results, self->AccIndex()));
        return true;
    }

    bool TryBuildPolymorphicNormalElementLoad(uint32_t bcIndex, ValueVertex *receiver, ValueVertex *key,
                                              const ValueLoadAccessSet &access)
    {
        if (access.elementCount < 2) {
            return false;
        }

        bool isMutantArrayEnabled = self->preproc_->GetEnv()->GetJSOptions().IsEnableMutantArray();
        std::vector<JSHClass *> expectedHClasses;
        std::vector<JSHClass *> compatibleHClasses;
        std::vector<ArkSteedHClassRef> compatibleHClassRefs;
        expectedHClasses.reserve(access.elementCount);
        compatibleHClasses.reserve(access.elementCount + 1);
        std::optional<bool> isJSArray;
        for (uint32_t i = 0; i < access.elementCount; ++i) {
            JSHClass *hclass = nullptr;
            if (!TryResolveNormalElementLoadHClass(access.elements[i], &hclass) ||
                std::find(expectedHClasses.begin(), expectedHClasses.end(), hclass) != expectedHClasses.end()) {
                return false;
            }
            if (isJSArray.has_value() && *isJSArray != hclass->IsJSArray()) {
                return false;
            }
            isJSArray = hclass->IsJSArray();
            expectedHClasses.push_back(hclass);
            if (!AppendCompatibleNormalElementHClasses(hclass, access.elements[i].expectedHClass, isMutantArrayEnabled,
                                                       &compatibleHClasses, &compatibleHClassRefs)) {
                return false;
            }
        }

        HClassCheckResult checkResult =
            BuildCheckAnyOfHClasses(bcIndex, receiver, compatibleHClasses, compatibleHClassRefs, false, false);
        if (checkResult != HClassCheckResult::SUCCESS) {
            return checkResult == HClassCheckResult::UNREACHABLE;
        }
        BuildNormalElementLoad(bcIndex, receiver, key);
        return true;
    }

    bool TryBuildElementLoad(uint32_t bcIndex, ValueVertex *receiver, ValueVertex *key,
                             const ValueLoadAccessSet &access)
    {
        if (access.kind != ValueLoadAccessKind::ELEMENT || access.elementCount == 0) {
            return false;
        }
        if (access.elementCount > 1) {
            switch (access.elements[0].kind) {
                case ElementLoadKind::NORMAL:
                    return TryBuildPolymorphicNormalElementLoad(bcIndex, receiver, key, access);
                case ElementLoadKind::TYPED_ARRAY:
                    return TryBuildPolymorphicTypedArrayElementLoad(bcIndex, receiver, key, access);
                default:
                    return false;
            }
        }
        const ElementLoadAccessInfo &accessInfo = access.elements[0];
        switch (accessInfo.kind) {
            case ElementLoadKind::NORMAL:
                return TryBuildNormalElementLoad(bcIndex, receiver, key, accessInfo);
            case ElementLoadKind::STRING:
                return TryBuildStringElementLoad(bcIndex, receiver, key, accessInfo);
            case ElementLoadKind::TYPED_ARRAY:
                return TryBuildTypedArrayElementLoad(bcIndex, receiver, key, accessInfo);
            default:
                return false;
        }
    }

    bool TryBuildLoadPropertyByValue(const BytecodeInfo *bcInfo, uint32_t bcIndex, ValueVertex *receiver,
                                     ValueVertex *key)
    {
        auto factory = self->pgoContext_.CreateAccessInfoFactory(bcInfo);
        ValueLoadAccessSet access;
        if (!factory.TryBuildValueLoadAccessInfo(&access)) {
            return false;
        }
        if (access.kind == ValueLoadAccessKind::ELEMENT) {
            return TryBuildElementLoad(bcIndex, receiver, key, access);
        }
        if (access.kind != ValueLoadAccessKind::NAMED) {
            return false;
        }

        std::optional<uint32_t> propertyKeyHandleIndex = GetHeapConstantNameHandleIndex(access.key);
        if (!propertyKeyHandleIndex.has_value()) {
            return false;
        }
        NamedLoadAccessInfosOpt accessInfos = TryGetLoadObjByNameAccessInfos(access.named, access.key);
        if (!accessInfos.has_value()) {
            return false;
        }
        bool hasStringHClass = std::any_of(accessInfos->begin(), accessInfos->end(), [](const auto &accessInfo) {
            return std::any_of(accessInfo.lookupStartObjectHClasses.begin(), accessInfo.lookupStartObjectHClasses.end(),
                               [](JSHClass *hclass) { return hclass != nullptr && hclass->IsString(); });
        });
        if (hasStringHClass) {
            return false;
        }

        BuildCheckValueKey(bcIndex, key, access.feedback.slotId);
        return TryBuildNamedAccessByValue(bcIndex, receiver, propertyKeyHandleIndex.value(), accessInfos.value());
    }

    static std::optional<AccessFieldRepresentation> TryConvertPropertyRepresentation(Representation representation)
    {
        switch (representation) {
            case Representation::TAGGED:
                return AccessFieldRepresentation::TAGGED;
            case Representation::INT:
                return AccessFieldRepresentation::INT32;
            case Representation::DOUBLE:
                return AccessFieldRepresentation::DOUBLE;
            default:
                return std::nullopt;
        }
    }

    bool TryBuildStoreNamedProperty(uint32_t bcIndex, ValueVertex *receiver, uint16_t constDataId, ValueVertex *value)
    {
        HClassInference inference(this, receiver, HClassInference::Mode::ALLOW_STALE);
        std::optional<NodeInfo::PossibleHClasses> possibleHClasses = inference.TryGetPossibleHClasses();
        std::optional<ArkSteedNameRef> nameRef = TryGetNameRefFromConstDataId(constDataId);
        if (!possibleHClasses.has_value() || !nameRef.has_value()) {
            return false;
        }

        bool hasPlan = false;
        bool isInlinedProperties = false;
        uint32_t fieldOffset = 0;
        AccessFieldRepresentation fieldRepresentation = AccessFieldRepresentation::UNKNOWN;
        for (JSHClass *hclass : possibleHClasses.value()) {
            std::optional<PropertyLookupResult> maybePlr = TryLookupPropertyInPGOHClass(hclass, nameRef.value());
            if (!maybePlr.has_value()) {
                return false;
            }
            PropertyLookupResult plr = maybePlr.value();
            std::optional<AccessFieldRepresentation> representation =
                TryConvertPropertyRepresentation(plr.GetRepresentation());
            if (!plr.IsFound() || !plr.IsLocal() || !plr.IsWritable() || plr.IsAccessor() ||
                !representation.has_value()) {
                return false;
            }
            if (!hasPlan) {
                hasPlan = true;
                isInlinedProperties = plr.IsInlinedProps();
                fieldOffset = plr.GetOffset();
                fieldRepresentation = representation.value();
                continue;
            }
            if (isInlinedProperties != plr.IsInlinedProps() || fieldOffset != plr.GetOffset() ||
                fieldRepresentation != representation.value()) {
                return false;
            }
        }
        if (!hasPlan) {
            return false;
        }

        std::vector<JSHClass *> pendingStableHClasses;
        pendingStableHClasses.reserve(possibleHClasses->size());
        bool needsNotPrototypeGuard = false;
        auto *broker = self->pgoContext_.GetBroker();
        for (JSHClass *hclass : possibleHClasses.value()) {
            bool isStable = JSTaggedValue(hclass).IsInSharedHeap();
            if (!isStable && self->IsLazyDeoptEnabled() &&
                !broker->TryInstallStableHClassDependency(hclass, &isStable)) {
                return false;
            }
            if (isStable) {
                pendingStableHClasses.push_back(hclass);
            }
            bool hasNotPrototypeDependency =
                self->IsLazyDeoptEnabled() && broker->TryInstallNotPrototypeDependency(hclass);
            needsNotPrototypeGuard |= !hasNotPrototypeDependency;
        }

        HClassCheckResult freshnessResult = inference.EnsureHClassesFresh(bcIndex);
        if (freshnessResult != HClassCheckResult::SUCCESS) {
            return freshnessResult == HClassCheckResult::UNREACHABLE;
        }
        for (JSHClass *hclass : pendingStableHClasses) {
            bool marked = compileInfoFacts_->MarkPossibleHClassStable(receiver, hclass);
            ASSERT(marked);
        }
        if (needsNotPrototypeGuard) {
            ChunkVector<ValueVertex *> guardInputs(self->chunk_);
            guardInputs.emplace_back(receiver);
            auto *guard = self->NewVertex<DeoptIfPrototypeChangedVertex>(
                currentBlock, guardInputs, self->chunk_, false, true, self->preproc_->GetBytecodeOffset(bcIndex));
            guard->SetEagerDeoptFrameState(BuildCurrentEagerDeoptFrameState(bcIndex));
        }

        ValueVertex *preparedValue = BuildCheckedNamedStoreValue(fieldRepresentation, value);
        ASSERT(preparedValue != nullptr);
        ValueVertex *storeTarget = receiver;
        int32_t storeOffset = static_cast<int32_t>(fieldOffset);
        if (!isInlinedProperties) {
            storeTarget = self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {receiver},
                                                                 static_cast<int32_t>(JSObject::PROPERTIES_OFFSET));
            storeOffset =
                static_cast<int32_t>(TaggedArray::DATA_OFFSET + fieldOffset * JSTaggedValue::TaggedTypeSize());
        }
        BuildPreparedNamedStoreField(storeTarget, storeOffset, preparedValue, fieldRepresentation,
                                     PropertyKey::ConstDataId(constDataId));
        return true;
    }

    std::optional<ArkSteedObjectRef> TryGetGlobalCellBox(const BytecodeInfo *bcInfo)
    {
        ArkSteedFeedbackReader reader(self->compilerThread_, bcInfo, self->pgoContext_.GetBroker());
        GlobalAccessFeedback feedback;
        if (!self->pgoContext_.GetBroker()->GetFeedbackForGlobalAccess(reader, &feedback)) {
            return std::nullopt;
        }
        return feedback.box;
    }

    ValueVertex *BuildGlobalCellBoxValue(uint32_t bcIndex, ValueVertex *box)
    {
        ValueVertex *value = self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {box},
                                                                    static_cast<int32_t>(PropertyBox::VALUE_OFFSET));
        ValueVertex *hole = self->graph_->GetTaggedConstant(JSTaggedValue::VALUE_HOLE);
        BuildCheckTaggedCondition(bcIndex, value, hole, Condition::EQUAL, kungfu::DeoptType::PROPERTYBOXINVALID);
        return value;
    }

    bool TryBuildLoadExternalModuleVar(ValueVertex *jsFunc, int32_t index)
    {
        if (!self->pgoContext_.GetBroker()->CanLoadExternalModuleVar(index, self->IsLazyDeoptEnabled())) {
            return false;
        }
        if (!self->IsLazyDeoptEnabled()) {
            ValueVertex *stage = self->NewVertex<LoadI32FromAddressVertex>(
                compileInfoFacts_, currentBlock, {glue},
                static_cast<int32_t>(JSThread::GlueData::GetStageOfHotReloadOffset(false)));
            ValueVertex *patchMain =
                self->graph_->GetInt32Constant(static_cast<int32_t>(StageOfHotReload::LOAD_END_EXECUTE_PATCHMAIN));
            BuildDeoptIfInt32Condition(stage, patchMain, Condition::EQUAL, kungfu::DeoptType::HOTRELOAD_PATCHMAIN);
        }

        int32_t moduleOffset = static_cast<int32_t>(JSFunction::ECMA_MODULE_OFFSET);
        ValueVertex *module =
            self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {jsFunc}, moduleOffset);
        int32_t environmentOffset = static_cast<int32_t>(SourceTextModule::SOURCE_TEXT_MODULE_OFFSET);
        ValueVertex *environment =
            self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {module}, environmentOffset);
        ValueVertex *binding = GetValueFromTaggedArray(environment, static_cast<uint32_t>(index));
        int32_t targetModuleOffset = static_cast<int32_t>(ResolvedIndexBinding::MODULE_OFFSET);
        ValueVertex *targetModule =
            self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {binding}, targetModuleOffset);
        int32_t targetIndexOffset = static_cast<int32_t>(ResolvedIndexBinding::INDEX_OFFSET);
        ValueVertex *targetIndex =
            self->NewVertex<LoadInt32FieldVertex>(compileInfoFacts_, currentBlock, {binding}, targetIndexOffset);
        int32_t dictionaryIndex = static_cast<int32_t>(SourceTextModule::NAME_DICTIONARY_OFFSET);
        ValueVertex *dictionary =
            self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {targetModule}, dictionaryIndex);
        ValueVertex *result =
            self->NewVertex<LoadTaggedElementVertex>(compileInfoFacts_, currentBlock, {dictionary, targetIndex});
        frameState.SetAcc(result);
        return true;
    }

    template <class VertexT>
    void UpdateCatchBlockData(VertexT *vertex)
    {
        ThrowableMixin *mixin = vertex;
        if (reinterpret_cast<uintptr_t>(lazyCatchBlock) == NO_CATCH_BLOCK_TAG) {
            return;  // (1) vertex has no catch block
        }
        if (blockInfo->catchBlockState == CatchBlockProfileState::NEVER_EXECUTED && self->IsLazyDeoptEnabled()) {
            if constexpr (std::is_base_of_v<LazyDeoptimizableMixin, VertexT>) {
                return;  // The catch block is represented by lazy-deopt frame state instead of compiled code.
            }
        }
        if (UNLIKELY(lazyCatchBlock == nullptr)) {
            lazyCatchBlock = self->ActivateCatchBlock(&lazyCatchBlockInputs, blockInfo->catchBlock->rpoIndex);
        }
        ASSERT(lazyCatchBlockInputs != nullptr);
        uint32_t catchPredIndex = lazyCatchBlockInputs->AddCatchPredecessor(frameState, self->undefinedValue_);
        lazyCatchBlockInputs->AddCompileInfoFacts(*compileInfoFacts_);
        mixin->LoadCatchBlock(lazyCatchBlock, catchPredIndex);
    }

    CallCommonStubVertex *CommonStubCall(std::initializer_list<ValueVertex *> inputs, CommonStubID id,
                                         SideEffectKind sideEffectKind = SideEffectKind::UNKNOWN_CALL)
    {
        ValidateCommonStubCallArgs(inputs, id);
        auto *vertex =
            self->NewVertex<CallCommonStubVertex>(compileInfoFacts_, currentBlock, inputs, id, sideEffectKind);
        UpdateCatchBlockData(vertex);
        return vertex;
    }

    CallCommonStubVertex *CommonStubCallWithLazyDeopt(std::initializer_list<ValueVertex *> inputs, CommonStubID id,
                                                      SideEffectKind sideEffectKind = SideEffectKind::UNKNOWN_CALL)
    {
        auto *vertex = CommonStubCall(inputs, id, sideEffectKind);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, vertex);
        return vertex;
    }

    CallCommonStubVertex *CommonStubCallToAccWithLazyDeopt(std::initializer_list<ValueVertex *> inputs, CommonStubID id,
                                                           SideEffectKind sideEffectKind = SideEffectKind::UNKNOWN_CALL)
    {
        auto *vertex = CommonStubCall(inputs, id, sideEffectKind);
        frameState.SetAcc(vertex);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, vertex);
        return vertex;
    }

    CallCommonStubVertex *CommonStubCallWithIC(const BytecodeInfo *bcInfo, std::initializer_list<ValueVertex *> inputs,
                                               CommonStubID id)
    {
        ChunkVector<ValueVertex *> allArgs(self->chunk_);
        allArgs.reserve(inputs.size() + CallVertex::FIRST_ARG_INDEX);

        allArgs.push_back(glue);
        allArgs.insert(allArgs.end(), inputs.begin(), inputs.end());
        allArgs.push_back(LoadParam(CALL_TARGET_PARAM_INDEX));
        allArgs.push_back(self->graph_->GetInt32Constant(GetICSlotId<int>(bcInfo, 0)));

        ValidateCommonStubCallArgs({allArgs.data(), allArgs.size()}, id);

        auto *vertex = self->NewVertex<CallCommonStubVertex>(compileInfoFacts_, currentBlock, allArgs, id);
        UpdateCatchBlockData(vertex);
        return vertex;
    }

    CallCommonStubVertex *CommonStubCallWithICAndLazyDeopt(const BytecodeInfo *bcInfo,
                                                           std::initializer_list<ValueVertex *> inputs, CommonStubID id)
    {
        auto *vertex = CommonStubCallWithIC(bcInfo, inputs, id);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, vertex);
        return vertex;
    }

    CallCommonStubVertex *CommonStubCallToAccWithICAndLazyDeopt(const BytecodeInfo *bcInfo,
                                                                std::initializer_list<ValueVertex *> inputs,
                                                                CommonStubID id)
    {
        auto *vertex = CommonStubCallWithIC(bcInfo, inputs, id);
        frameState.SetAcc(vertex);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, vertex);
        return vertex;
    }

    template <class InputRange = std::initializer_list<ValueVertex *>>
    CallRuntimeVertex *RuntimeCall(const InputRange &inputs, RuntimeStubID id,
                                   SideEffectKind sideEffectKind = SideEffectKind::UNKNOWN_CALL)
    {
        auto *vertex = self->NewVertex<CallRuntimeVertex>(compileInfoFacts_, currentBlock, inputs, id, sideEffectKind);
        UpdateCatchBlockData(vertex);
        return vertex;
    }

    template <class InputRange = std::initializer_list<ValueVertex *>>
    CallRuntimeVertex *RuntimeCallToAccWithLazyDeopt(const InputRange &inputs, RuntimeStubID id,
                                                     SideEffectKind sideEffectKind = SideEffectKind::UNKNOWN_CALL)
    {
        auto *vertex = RuntimeCall(inputs, id, sideEffectKind);
        frameState.SetAcc(vertex);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, vertex);
        return vertex;
    }

    template <class InputRange = std::initializer_list<ValueVertex *>>
    CallRuntimeVertex *RuntimeCallWithLazyDeopt(const InputRange &inputs, RuntimeStubID id,
                                                SideEffectKind sideEffectKind = SideEffectKind::UNKNOWN_CALL)
    {
        auto *vertex = RuntimeCall(inputs, id, sideEffectKind);
        LoadLazyDeoptFrameStateForThrowableCall(currentBcIndex, vertex);
        return vertex;
    }

    ValueVertex *TaggedConstantFromInt32(int value)
    {
        JSTaggedType taggedValue = JSTaggedValue(value).GetRawData();
        return self->graph_->GetTaggedConstant(taggedValue);
    }

    ValueVertex *TaggedArrayFromValueIn(const BytecodeInfo *bcInfo, ValueVertex *taggedInputSize, uint32_t inputSize,
                                        uint32_t startIndex = 0)
    {
        ValueVertex *taggedArray = RuntimeCall({taggedInputSize}, RTSTUB_ID(NewTaggedArray));
        for (uint32_t idx = 0; idx < inputSize; ++idx) {
            ValueVertex *arg = LoadRegister(bcInfo, startIndex + idx);
            SetValueToTaggedArray(taggedArray, idx, arg);
        }
        return taggedArray;
    }

    ValueVertex *GetValueFromTaggedArray(ValueVertex *array, uint32_t index)
    {
        int32_t offset = static_cast<int32_t>(TaggedArray::DATA_OFFSET + index * JSTaggedValue::TaggedTypeSize());
        return self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {array}, offset);
    }

    void SetValueToTaggedArray(ValueVertex *array, uint32_t index, ValueVertex *value)
    {
        int32_t offset = static_cast<int32_t>(TaggedArray::DATA_OFFSET + index * JSTaggedValue::TaggedTypeSize());
        BuildStoreTaggedField(array, offset, value);
    }

    ValueVertex *SharedConstPool()
    {
        int32_t methodOffset = static_cast<int32_t>(JSFunctionBase::METHOD_OFFSET);
        int32_t constpoolOffset = static_cast<int32_t>(Method::CONSTANT_POOL_OFFSET);

        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        ValueVertex *method =
            self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {jsFunc}, methodOffset);
        return self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {method}, constpoolOffset);
    }

    ValueVertex *ModuleFromFunction()
    {
        int32_t moduleOffset = static_cast<int32_t>(JSFunction::ECMA_MODULE_OFFSET);
        ValueVertex *jsFunc = LoadParam(CALL_TARGET_PARAM_INDEX);
        return self->NewVertex<LoadTaggedFieldVertex>(compileInfoFacts_, currentBlock, {jsFunc}, moduleOffset);
    }

    bool TryGetConstPoolIndex(ValueVertex *indexVertex, uint32_t *index) const
    {
        ASSERT(index != nullptr);
        if (auto *constant = indexVertex->TryCast<Int32ConstantVertex>()) {
            if (constant->GetValue() < 0) {
                return false;
            }
            *index = static_cast<uint32_t>(constant->GetValue());
            return true;
        }
        if (auto *constant = indexVertex->TryCast<TaggedConstantVertex>()) {
            JSTaggedValue value(constant->GetValue());
            if (!value.IsInt() || value.GetInt() < 0) {
                return false;
            }
            *index = static_cast<uint32_t>(value.GetInt());
            return true;
        }
        return false;
    }

    ValueVertex *StringFromConstPool(ValueVertex *stringId)
    {
        uint32_t index = 0;
        ArkSteedHeapBroker *broker = self->pgoContext_.GetBroker();
        if (TryGetConstPoolIndex(stringId, &index) && index <= std::numeric_limits<uint16_t>::max() &&
            broker != nullptr) {
            ArkSteedNameRef stringRef;
            if (broker->TryGetNameFromConstantPool(static_cast<uint16_t>(index), &stringRef)) {
                if (ValueVertex *constant = GetHeapConstant(stringRef); constant != nullptr) {
                    return constant;
                }
            }
        }
        ValueVertex *constpool = SharedConstPool();
        return CommonStubCall({glue, constpool, stringId}, CommonStubID::GetStringFromConstPool,
                              SideEffectKind::SAFE_CALL);
    }

    ValueVertex *ObjectFromConstPool(ValueVertex *index)
    {
        ValueVertex *constpool = SharedConstPool();
        ValueVertex *module = ModuleFromFunction();
        return CommonStubCall({glue, constpool, index, module}, CommonStubID::GetObjectFromConstPool);
    }

    ValueVertex *MethodFromConstPool(ValueVertex *index)
    {
        uint32_t methodIndex = 0;
        ArkSteedHeapBroker *broker = self->pgoContext_.GetBroker();
        if (TryGetConstPoolIndex(index, &methodIndex) && methodIndex <= std::numeric_limits<uint16_t>::max() &&
            broker != nullptr) {
            ArkSteedObjectRef methodRef;
            if (broker->TryGetMethodFromConstantPool(static_cast<uint16_t>(methodIndex), &methodRef)) {
                if (ValueVertex *constant = GetHeapConstant(methodRef); constant != nullptr) {
                    return constant;
                }
            }
        }
        ValueVertex *constpool = SharedConstPool();
        return RuntimeCall({constpool, index}, RTSTUB_ID(GetMethodFromCache));
    }

    GraphBuilder *self;
    ValueVertex *glue;           // Equivalent to self->glue_. Cached for performance.
    ValueVertex *lazyGlobalEnv;  // Equivalent to self->lazyGlobalEnv_. Cached for performance.
    const BasicBlockInfo *blockInfo;
    BB *currentBlock;    // Equivalent to self->blocks_[blockInfo->rpoIndex]. Cached for performance.
    BB *lazyCatchBlock;  // Equivalent to self->blocks_[blockInfo->catchBlock->rpoIndex]. Cached for performance.
    CompileInfoFacts *compileInfoFacts_;  // Equivalent to self->compileInfoFacts_[blockInfo->rpoIndex].
    SharedBCFrameState frameState;
    CatchBlockInputData *lazyCatchBlockInputs;
    const BytecodeInfo *currentBcInfo {nullptr};
    uint32_t currentBcIndex {0};
};

BB *GraphBuilder::VisitBytecodesOfBasicBlock(SharedBCFrameState frameState, uint32_t rpoIndex,
                                             CompileInfoFacts **outFacts)
{
    const BasicBlockInfo *blockInfo = preproc_->GetBasicBlockByRPO(rpoIndex);

    BB *catchBlock = reinterpret_cast<BB *>(NO_CATCH_BLOCK_TAG);
    CatchBlockInputData *caughtByData = reinterpret_cast<CatchBlockInputData *>(NO_CATCH_BLOCK_TAG);
    if (blockInfo->catchBlock != nullptr) {
        uint32_t catchBlockIndex = blockInfo->catchBlock->rpoIndex;
        // May be nullptr (indicating that the catch block is not activated yet)
        catchBlock = BlockAt(catchBlockIndex);
        caughtByData = CatchBlockInputsAt(catchBlockIndex);
    }

    BytecodeVisitor visitor {
        .self = this,
        .glue = glue_,
        .lazyGlobalEnv = lazyGlobalEnv_,
        .blockInfo = blockInfo,
        .currentBlock = BlockAt(rpoIndex),  // visitor.currentBlock may be updated by subgraph creation
        .lazyCatchBlock = catchBlock,
        .compileInfoFacts_ = CompileInfoFactsAt(rpoIndex),
        .frameState = frameState,
        .lazyCatchBlockInputs = caughtByData,
    };
    for (uint32_t bcIndex = blockInfo->startBcIndex; bcIndex <= blockInfo->endBcIndex; ++bcIndex) {
        // The previous Visit has returned: its raw heap temporaries are dead.
        // Persistent compiler state contains refs/handles or rooted non-moving
        // HClasses, not movable object addresses.
        static_cast<JitThread *>(compilerThread_)->HeapSafepoint();
        if (!visitor.Visit(preproc_->GetBytecode(bcIndex), bcIndex)) {
            break;
        }
    }
    compileInfoFacts_[rpoIndex] = visitor.compileInfoFacts_;

    if (visitor.currentBlock->GetControlVertex() == nullptr) {
        ASSERT(blockInfo->IsFallthrough());
        BB *target = ActivateNonCatchBlock(blockInfo->fallthroughBlock->rpoIndex);
        FinishBlockWithJump(visitor.currentBlock, target);
    }
    *outFacts = visitor.compileInfoFacts_;
    // Lowering may create internal subgraphs, so callers need the actual exit block rather than blocks_[rpoIndex].
    return visitor.currentBlock;
}
}  // namespace panda::ecmascript::arksteed
