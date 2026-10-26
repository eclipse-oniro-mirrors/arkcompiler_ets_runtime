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

#ifndef ECMASCRIPT_ARKSTEED_ARKSTEED_GRAPH_VERIFIER_H
#define ECMASCRIPT_ARKSTEED_ARKSTEED_GRAPH_VERIFIER_H

#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_graph_processor.h"
#include "ecmascript/arksteed/arksteed_vertex.h"
#include "ecmascript/js_arguments.h"
#include "ecmascript/js_array.h"
#include "ecmascript/mem/chunk_containers.h"
#include "ecmascript/mem/mem.h"

namespace panda::ecmascript::arksteed {

// The uninitialized allocation contract is a checked allocation diamond:
//
//   entry: CanAllocateInYoung --BranchIfTrue--> fastBlock
//       \____________________________--> slowBlock (deferred)
//   fastBlock: AllocateInYoung (bump)     --> Jump(mergeBlock)
//   slowBlock: CallRuntime AllocateInYoung --> Jump(mergeBlock)
//   mergeBlock: Phi(fast, slow) --> InitializeXXX --> rest
//
// The uninitialized allocation may only feed the merge Phi, the merge Phi may only feed
// the matching initializer, and no GC-capable call, safepoint, deopt or exception
// exit may sit between raw storage and initialization. Gap moves inserted by
// register allocation are the only permitted intervening vertices. The arguments
// object initializer internally permits only non-GC write-barrier calls.
class ArkSteedGraphVerifier {
public:
    explicit ArkSteedGraphVerifier(Chunk* chunk, bool afterRegalloc = false)
        : seen_(chunk), allocationBlocks_(chunk), afterRegalloc_(afterRegalloc)
    {
    }

    void PreProcessGraph(Graph *graph)
    {
        seen_.resize(graph->MaxBlockId());
    }

    void PostProcessGraph(Graph* graph)
    {
        for (const auto& allocation: allocationBlocks_) {
            ASSERT(allocation.second == nullptr && "Allocation requires a merge and initializer");
        }
    }

    void PostProcessBlock(BB* block)
    {
        if (pendingAllocation_ != nullptr && IsUninitializedAllocation(pendingAllocation_)) {
            // Allocation blocks must hand their raw storage to the merge block.
            ASSERT(block->GetControlVertex() != nullptr
                   && "Allocation block must end with a jump to the merge block");
            pendingAllocation_ = nullptr;
            return;
        }
        ASSERT(pendingAllocation_ == nullptr && "Incomplete initialization interval");
    }

    void PreProcessBlock(BB *block)
    {
        // Check Ids are unique.
        ASSERT(seen_[block->GetId()] == 0);
        seen_[block->GetId()] = 1;
    }

    void PostPhiProcessing() {}

    // ProcessVertex is called via GraphProcessor's opcode-based dispatch.
    // The VertexT template parameter is deduced as the concrete vertex type
    // (e.g., JumpVertex, CallVertex, PhiVertex), not the base ControlVertex
    // or NonControlVertex types. This ensures VerifyInputs() is resolved
    // correctly for each specific vertex type.
    //
    // VerifyInputs() implementation coverage:
    //
    // 1. FixedInputVertexMixin - Base template providing VerifyInputs() for all
    //    fixed-input vertices. Vertex::New validates the input count against
    //    NUM_INPUTS at construction; VerifyInputs checks input types if
    //    INPUT_TYPES is defined. Fixed-input vertices inherit this via CRTP.
    //
    // 2. Other vertex-specific checks include:
    //    - CallVertex: Validates FIRST_ARG_INDEX + actualArgc_ inputs
    //      (target, new target, this and arguments)
    //    - CallRuntimeVertex: For uninitialized allocation, validates the tagged integer
    //      size and absence of recovery metadata; glue comes from the call ABI
    //    - CallCommonStubVertex: Validates GetInputCount() >= 1 (minimum glue
    //      parameter required for common stubs)
    //    - FastSuperAllocateThisVertex: Validates GetInputCount() == 3 (glue,
    //      superCtor, newTarget)
    //    - DeoptVertex: Has no SSA inputs; eager frame values are stored separately
    //    - ReturnVertex: Uses the fixed-input verifier for the return value
    //    - ThrowVertex: Variable-input vertex; input count depends on runtime stub
    //    - PhiVertex: Validates GetInputCount() > 0 and all inputs have
    //      consistent ValueRepresentation (type consistency at SSA merge)
    //
    // All vertices are covered because ALL_VERTEX_LIST includes every opcode,
    // ensuring the switch-case in GraphProcessor dispatches to all types.
    // Fixed-input vertices use the inherited implementation from FixedInputVertexMixin;
    // other vertices provide checks for their semantic requirements.
    template <typename VertexT>
    void ProcessVertex(VertexT* vertex, const ArkSteedState& state)
    {
        // Frame values are stored separately from SSA inputs and must be checked even
        // outside the interval (including users in earlier traversal blocks).
        if constexpr (std::is_base_of_v<EagerDeoptimizableMixin, VertexT>
                      || std::is_base_of_v<LazyDeoptimizableMixin, VertexT>) {
            for (uint32_t i = 0; i < vertex->GetDeoptFrameValueCount(); ++i) {
                auto* value = vertex->GetDeoptFrameValue(i);
                ASSERT(value != nullptr);
                ASSERT(!IsUninitializedAllocation(value) && !IsAllocationPhi(value)
                       && "Allocation escapes into frame state");
            }
        }
        // Verify all inputs have valid opcodes
        for (uint32_t i = 0, n = vertex->GetInputCount(); i < n; i++) {
            ValueVertex *input = vertex->GetInput(i);
            ASSERT(input != nullptr);
            VertexOpcode opcode = input->GetOpcode();
            ASSERT(opcode >= FIRST_OPCODE);
            ASSERT(opcode <= LAST_OPCODE);
            if (IsUninitializedAllocation(input)) {
                // Raw storage may only be consumed by the merge Phi; the Phi is
                // validated by TryRegisterAllocationPhi below.
                ASSERT((std::is_same_v<VertexT, PhiVertex>)&&"Allocation escapes initialization");
            }
            if (IsAllocationPhi(input)) {
                if constexpr (IsInitializer<VertexT>()) {
                    ASSERT(i == VertexT::ALLOCATION_INDEX && input == pendingAllocation_
                           && "Allocation Phi may only feed its matching initializer");
                } else {
                    ASSERT(false && "Allocation Phi escapes initialization");
                }
            }
        }
        vertex->VerifyInputs();
        if constexpr (std::is_same_v<VertexT, PhiVertex>) {
            TryRegisterAllocationPhi(vertex, state);
        }
        VerifyInitializationInterval(vertex, state);
    }

private:
    // Identify raw-storage Phis structurally, including uses visited before the Phi.
    static bool IsAllocationPhi(const ValueVertex* value)
    {
        if (auto* phi = value->TryCast<PhiVertex>()) {
            for (uint32_t i = 0; i < phi->GetInputCount(); ++i) {
                auto* input = phi->GetInput(i);
                if (input != nullptr && IsUninitializedAllocation(input)) {
                    return true;
                }
            }
        }
        return false;
    }

    template<typename VertexT>
    static constexpr bool IsInitializer()
    {
        return std::is_same_v<VertexT, InitializeEmptyObjectVertex>
               || std::is_same_v<VertexT, InitializeObjectWithBufferVertex>
               || std::is_same_v<VertexT, InitializeEmptyArrayVertex>
               || std::is_same_v<VertexT, InitializeArrayWithBufferVertex>
               || std::is_same_v<VertexT, InitializeUnmappedArgumentsElementsVertex>
               || std::is_same_v<VertexT, InitializeUnmappedArgumentsVertex>;
    }

    static void VerifyStaticSize(uint64_t size, uint32_t minimumSize)
    {
        ASSERT(size >= minimumSize);
        ASSERT(size <= MAX_REGULAR_HEAP_OBJECT_SIZE);
        ASSERT(size % JSTaggedValue::TaggedTypeSize() == 0);
    }

    static bool IsInt64Constant(ValueVertex* value, int64_t expected)
    {
        auto* constant = value->TryCast<Int64ConstantVertex>();
        return constant != nullptr && constant->GetValue() == expected;
    }

    static void VerifyArgumentCount(ValueVertex* argc)
    {
        if (auto* constant = argc->TryCast<Int64ConstantVertex>()) {
            ASSERT(constant->GetValue() >= NUM_MANDATORY_JSFUNC_ARGS);
            ASSERT(constant->GetValue() - NUM_MANDATORY_JSFUNC_ARGS <= INT32_MAX);
        } else {
            // Only the frame ABI supplies a nonnegative, INT32 user count.
            ASSERT(argc->Is<ActualArgcVertex>() && "Arguments require the actual frame count");
        }
    }

    static void VerifyArgumentsElementsSize(ValueVertex* size, ValueVertex* argc)
    {
        VerifyArgumentCount(argc);
        if (auto* constant = size->TryCast<Int64ConstantVertex>()) {
            auto* count = argc->Cast<Int64ConstantVertex>();
            int64_t expected = (count->GetValue() - NUM_MANDATORY_JSFUNC_ARGS) * JSTaggedValue::TaggedTypeSize()
                               + TaggedArray::DATA_OFFSET;
            ASSERT(constant->GetValue() == expected && "Arguments elements size must match the count");
            return;
        }
        // Match the full-width formula, not a narrowed copy of either the count
        // or the size: ((argc - mandatory) << slotShift) + dataOffset.
        auto* add = size->Cast<I64AddVertex>();
        ASSERT(IsInt64Constant(add->GetInput(I64AddVertex::RIGHT_INDEX), TaggedArray::DATA_OFFSET));
        auto* shift = add->GetInput(I64AddVertex::LEFT_INDEX)->Cast<I64ShiftLeftVertex>();
        ASSERT(shift->GetShift() == TAGGED_TYPE_SIZE_LOG);
        auto* subtract = shift->GetInput(I64ShiftLeftVertex::INPUT_INDEX)->Cast<I64SubVertex>();
        ASSERT(subtract->GetInput(I64SubVertex::LEFT_INDEX) == argc
               && "Arguments elements size must match the count");
        ASSERT(IsInt64Constant(subtract->GetInput(I64SubVertex::RIGHT_INDEX), NUM_MANDATORY_JSFUNC_ARGS));
    }

    static void VerifyTaggedSize(ValueVertex* taggedSize, ValueVertex* size)
    {
        if (auto* constant = taggedSize->TryCast<TaggedConstantVertex>()) {
            auto* bytes = size->Cast<Int64ConstantVertex>();
            ASSERT(JSTaggedValue(constant->GetValue()).GetLargeUInt() == static_cast<uint64_t>(bytes->GetValue())
                   && "Allocation must match the checked size");
            return;
        }
        auto* raw = taggedSize->Cast<RawI64ToTaggedVertex>();
        auto* bits = raw->GetInput(RawI64ToTaggedVertex::INPUT_INDEX);
        if (auto* constant = bits->TryCast<Int64ConstantVertex>()) {
            // Keep the proof valid if the size/tag OR has been constant-folded.
            auto* bytes = size->Cast<Int64ConstantVertex>();
            ASSERT(static_cast<uint64_t>(constant->GetValue())
                       == (static_cast<uint64_t>(bytes->GetValue()) | JSTaggedValue::TAG_INT)
                   && "Allocation must match the checked size");
            return;
        }
        auto* encoding = bits->Cast<I64BitwiseBinaryVertex>();
        ASSERT(encoding->GetKind() == IntBitwiseKind::BITWISE_OR);
        ASSERT(encoding->GetInput(I64BitwiseBinaryVertex::LEFT_INDEX) == size
               && "Allocation must match the checked size");
        ASSERT(IsInt64Constant(encoding->GetInput(I64BitwiseBinaryVertex::RIGHT_INDEX),
                               static_cast<int64_t>(JSTaggedValue::TAG_INT)));
    }

    // Records the merge Phi of the fast and slow allocation blocks and makes it
    // the pending value the initializer must consume in this block.
    void TryRegisterAllocationPhi(PhiVertex* phi, const ArkSteedState& state)
    {
        if (!IsAllocationPhi(phi)) {
            return;
        }
        // 2: one fast and one slow allocation input
        ASSERT(phi->GetInputCount() == 2 && "Allocation merge Phi requires exactly one fast and one slow input");
        auto* fast = phi->GetInput(0)->TryCast<AllocateInYoungVertex>();
        auto* slow = phi->GetInput(1)->TryCast<CallRuntimeVertex>();
        if (fast == nullptr || slow == nullptr) {
            fast = phi->GetInput(1)->TryCast<AllocateInYoungVertex>();
            slow = phi->GetInput(0)->TryCast<CallRuntimeVertex>();
        }
        ASSERT(fast != nullptr && slow != nullptr && slow->ReturnsUninitializedAllocation()
               && "Allocation merge Phi requires one fast and one slow allocation");
        ASSERT(allocationBlocks_.count(fast) != 0 && allocationBlocks_.count(slow) != 0);
        BB* mergeBlock = state.GetBlock();
        ASSERT(pendingAllocation_ == nullptr && "Second allocation in initialization interval");
        // 2: fast-path and slow-path blocks
        ASSERT(mergeBlock->GetPredecessors().size() == 2 && "Allocation merge requires two predecessors");
        for (uint32_t i = 0; i < 2; ++i) {  // 2: fast and slow inputs
            auto* allocation = phi->GetInput(i);
            auto* block = allocationBlocks_.at(allocation);
            ASSERT(block != nullptr && "Allocation has multiple merge Phis");
            ASSERT(block == mergeBlock->GetPredecessor(i) && "Allocation Phi input must match its predecessor");
            auto* jump = block->GetControlVertex()->Cast<JumpVertex>();
            ASSERT(jump->Target() == mergeBlock && "Allocation must jump to its merge Phi block");
        }
        ASSERT(allocationBlocks_.at(fast)->GetPredecessor(0) == allocationBlocks_.at(slow)->GetPredecessor(0)
               && "Allocation paths must share the capacity check");
        allocationBlocks_.at(fast) = nullptr;
        allocationBlocks_.at(slow) = nullptr;
        pendingAllocation_ = phi;
    }

    template<typename VertexT>
    void VerifyInitializer(VertexT* vertex)
    {
        static_assert(!IsCall(VertexT::PROPERTIES) && !CanAllocate(VertexT::PROPERTIES)
                      && !CanDeopt(VertexT::PROPERTIES) && !CanThrow(VertexT::PROPERTIES));
        // The arguments object uses only non-GC write-barrier calls. Its emitter
        // preserves the working registers and has no safepoint or recovery exit.
        // This exception is local to that initializer, not all deferred calls.
        static_assert(!IsDeferredCall(VertexT::PROPERTIES)
                      || std::is_same_v<VertexT, InitializeUnmappedArgumentsVertex>);
        ASSERT(pendingAllocation_ != nullptr && "Initializer without allocation in this block");
        ASSERT(vertex->GetInput(VertexT::ALLOCATION_INDEX) == pendingAllocation_);
        // The pending Phi has already passed TryRegisterAllocationPhi.
        auto* phi = pendingAllocation_->template Cast<PhiVertex>();
        auto* fast = phi->GetInput(0)->TryCast<AllocateInYoungVertex>();
        if (fast == nullptr) {
            fast = phi->GetInput(1)->Cast<AllocateInYoungVertex>();
        }
        if constexpr (!std::is_same_v<VertexT, InitializeUnmappedArgumentsElementsVertex>) {
            ASSERT(vertex->GetInput(VertexT::GLUE_INDEX) == fast->GetInput(AllocateInYoungVertex::GLUE_INDEX));
        }
        auto* size = fast->GetInput(AllocateInYoungVertex::SIZE_INDEX);
        if constexpr (std::is_same_v<VertexT, InitializeUnmappedArgumentsElementsVertex>) {
            // This installs the array header. LowerGetUnmappedArgs then fills
            // every element in its safepoint-free copy loop before allocating
            // the arguments object; the loop's barriers are non-GC calls.
            VerifyArgumentsElementsSize(size, vertex->GetInput(VertexT::NUM_ARGS_INDEX));
        } else if constexpr (std::is_same_v<VertexT, InitializeUnmappedArgumentsVertex>) {
            auto* argc = vertex->GetInput(VertexT::NUM_ARGS_INDEX);
            VerifyArgumentCount(argc);
            auto* elements = vertex->GetInput(VertexT::ELEMENTS_INDEX)
                                 ->template Cast<InitializeUnmappedArgumentsElementsVertex>();
            ASSERT(elements->GetInput(InitializeUnmappedArgumentsElementsVertex::NUM_ARGS_INDEX) == argc
                   && "Arguments object and elements must use the same count");
            constexpr uint32_t objectSize
                = JSArguments::GetInlinedPropertyOffset(JSArguments::LENGTH_OF_INLINE_PROPERTIES);
            static_assert(JSHClass::DEFAULT_CAPACITY_OF_IN_OBJECTS == JSArguments::LENGTH_OF_INLINE_PROPERTIES);
            ASSERT(IsInt64Constant(size, objectSize) && "Arguments object requires its static layout size");
        } else {
            VerifyLiteralInitializer(vertex, size);
        }
        pendingAllocation_ = nullptr;
    }

    template<typename VertexT>
    static void VerifyLiteralInitializer(VertexT* vertex, ValueVertex* size)
    {
        uint32_t objectSize;
        uint32_t rootIndex;
        if constexpr (std::is_same_v<VertexT, InitializeObjectWithBufferVertex>) {
            const auto& info = vertex->GetInfo();
            objectSize = info.objectSize;
            rootIndex = VertexT::LITERAL_INDEX;
            VerifyStaticSize(objectSize, JSObject::SIZE);
            // Raw numeric bits are not tagged values. The broker proves the snapshot contains no roots.
        } else {
            objectSize = vertex->GetObjectSize();
            if constexpr (std::is_same_v<VertexT, InitializeEmptyObjectVertex>) {
                rootIndex = VertexT::HCLASS_INDEX;
                VerifyStaticSize(objectSize, JSObject::SIZE);
                ASSERT(vertex->GetInput(VertexT::SIZE_INDEX) == size);
            } else {
                VerifyStaticSize(objectSize, JSArray::SIZE + JSTaggedValue::TaggedTypeSize());
                if constexpr (std::is_same_v<VertexT, InitializeEmptyArrayVertex>) {
                    rootIndex = VertexT::GLOBAL_ENV_INDEX;
                } else {
                    rootIndex = VertexT::LITERAL_INDEX;
                }
            }
        }
        ASSERT(IsInt64Constant(size, objectSize) && "Literal allocation requires its static size");
        // These inputs are broker-protected heap constants, not raw pointers hidden in metadata.
        ASSERT(vertex->GetInput(rootIndex)->template Is<HeapConstantVertex>()
               && "Initializer requires a rooted constant");
    }

    template<typename VertexT>
    void VerifyInitializationInterval(VertexT* vertex, const ArkSteedState& state)
    {
        if constexpr (std::is_same_v<VertexT, AllocateInYoungVertex>
                      || std::is_same_v<VertexT, CallRuntimeVertex>) {
            if (IsUninitializedAllocation(vertex)) {
                VerifyUninitializedAllocation(vertex, state);
                pendingAllocation_ = vertex;
                return;
            }
        }
        if constexpr (IsInitializer<VertexT>()) {
            VerifyInitializer(vertex);
        } else {
            if constexpr (std::is_same_v<VertexT, GapMoveVertex>
                          || std::is_same_v<VertexT, ConstantGapMoveVertex>) {
                static_assert(!IsCall(VertexT::PROPERTIES) && !IsDeferredCall(VertexT::PROPERTIES)
                              && !CanAllocate(VertexT::PROPERTIES) && !CanDeopt(VertexT::PROPERTIES)
                              && !CanThrow(VertexT::PROPERTIES));
            }
            if constexpr (std::is_same_v<VertexT, ConstantGapMoveVertex>) {
                // ConstantGapMove has no SSA inputs; its source is a separate member.
                ASSERT(vertex->GetVertex() != nullptr);
                ASSERT(IsConstantValueVertex(vertex->GetVertex()->GetOpcode()));
            }
            if (pendingAllocation_ != nullptr) {
                // Spill reloads are GapMove vertices, not arbitrary field loads. No other
                // operation, even a pure one, is allowed to interrupt initialization.
                // The merge Phi registers itself just above and must not trip this check.
                constexpr bool isMove
                    = std::is_same_v<VertexT, GapMoveVertex> || std::is_same_v<VertexT, ConstantGapMoveVertex>;
                constexpr bool isMergeJump = std::is_same_v<VertexT, JumpVertex>;
                constexpr bool isPhiType = std::is_same_v<VertexT, PhiVertex>;
                bool isRegisteredPhi = isPhiType && pendingAllocation_ == static_cast<Vertex*>(vertex);
                ASSERT((isRegisteredPhi || (afterRegalloc_ && isMove)
                        || (isMergeJump && IsUninitializedAllocation(pendingAllocation_)))
                       && "Illegal operation in initialization interval");
            }
        }
    }

    template<typename VertexT>
    void VerifyUninitializedAllocation(VertexT* vertex, const ArkSteedState& state)
    {
        static_assert(!IsDeferredCall(VertexT::PROPERTIES) && !CanEagerDeopt(VertexT::PROPERTIES));
        auto* block = state.GetBlock();
        ASSERT(pendingAllocation_ == nullptr && "Second allocation in initialization interval");
        ASSERT(allocationBlocks_.insert({vertex, block}).second && "Allocation scheduled more than once");
        ASSERT(block->GetPredecessors().size() == 1 && "Allocation requires one checking predecessor");
        auto* checkBlock = block->GetPredecessor(0);
        auto* control = checkBlock->GetControlVertex();
        ASSERT(control != nullptr && "Allocation requires a capacity check");
        auto* branch = control->template Cast<BranchIfTrueVertex>();
        auto* check = branch->GetInput(0)->template TryCast<CanAllocateInYoungVertex>();
        ASSERT(check != nullptr && "Allocation requires a capacity predicate");
        // A boolean from another block or from before an intervening operation
        // cannot prove that the current bump pointer still has enough capacity.
        bool foundCheck = false;
        for (auto* preceding: checkBlock->GetVertices()) {
            if (preceding == check) {
                ASSERT(!foundCheck && "Capacity predicate scheduled more than once");
                foundCheck = true;
            } else if (foundCheck) {
                ASSERT(afterRegalloc_ && (preceding->Is<GapMoveVertex>() || preceding->Is<ConstantGapMoveVertex>())
                       && "Only register moves may separate the capacity predicate and branch");
            }
        }
        ASSERT(foundCheck && "Capacity predicate must be scheduled in the checking block");
        auto* size = check->GetInput(CanAllocateInYoungVertex::SIZE_INDEX);
        ASSERT(size->GetValueRepresentation() == ValueRepresentation::INT64);
        if (auto* constant = size->template TryCast<Int64ConstantVertex>()) {
            auto bytes = static_cast<uint64_t>(constant->GetValue());
            ASSERT(bytes >= TaggedArray::DATA_OFFSET && (bytes & JSTaggedValue::TAG_MARK) == 0);
            ASSERT(bytes % JSTaggedValue::TaggedTypeSize() == 0);
        }
        // The matching initializer proves the size's source and layout. Literal
        // sizes remain regular constants; argument elements may be huge.
        if constexpr (std::is_same_v<VertexT, AllocateInYoungVertex>) {
            static_assert(!IsCall(VertexT::PROPERTIES) && !CanDeopt(VertexT::PROPERTIES)
                          && !CanThrow(VertexT::PROPERTIES) && CanAllocate(VertexT::PROPERTIES));
            ASSERT(check->GetInput(CanAllocateInYoungVertex::GLUE_INDEX) == vertex->GetInput(VertexT::GLUE_INDEX)
                   && size == vertex->GetInput(VertexT::SIZE_INDEX)
                   && "Allocation must match the checked glue and size");
            ASSERT(branch->IfTrue() == block && "Fast allocation requires the successful check edge");
        } else {
            static_assert(IsCall(VertexT::PROPERTIES) && CanAllocate(VertexT::PROPERTIES));
            // Runtime argv starts with tagged size and may include HClass; glue comes from the call ABI.
            VerifyTaggedSize(vertex->GetInput(0), size);
            ASSERT(branch->IfFalse() == block && "Slow allocation requires the failed check edge");
        }
        ASSERT(!block->HasPhi() && "Allocation block must preserve checked inputs");
        for (auto* preceding: block->GetVertices()) {
            if (preceding == vertex) {
                break;
            }
            ASSERT(afterRegalloc_ && (preceding->Is<GapMoveVertex>() || preceding->Is<ConstantGapMoveVertex>())
                   && "Only register moves may separate the check and allocation");
        }
    }

    ChunkVector<uint8_t> seen_;
    // Keep each allocation's block until its merge Phi consumes it, then clear the value.
    ChunkMap<ValueVertex*, BB*> allocationBlocks_;
    ValueVertex* pendingAllocation_ {nullptr};
    [[maybe_unused]] bool afterRegalloc_;
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_ARKSTEED_GRAPH_VERIFIER_H
