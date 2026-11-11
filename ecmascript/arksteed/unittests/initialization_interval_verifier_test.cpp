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

#include "ecmascript/arksteed/arksteed_graph_verifier.h"
#include "ecmascript/js_arguments.h"
#include "ecmascript/js_array.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/tests/test_helper.h"

namespace panda::ecmascript::arksteed {

#ifndef NDEBUG
class ArkSteedInitializationIntervalVerifierTest : public testing::Test {
protected:
    enum Initializer { EMPTY_OBJECT, OBJECT_WITH_BUFFER, EMPTY_ARRAY, ARRAY_WITH_BUFFER };
    static constexpr Initializer INITIALIZERS[]
        = {EMPTY_OBJECT, OBJECT_WITH_BUFFER, EMPTY_ARRAY, ARRAY_WITH_BUFFER};

    // Builds the literal allocation contract used by the four creations:
    //
    //   entry: CanAllocateInYoung --BranchIfTrue--> fastBlock
    //       \____________________________--> slowBlock
    //   fastBlock: AllocateInYoung (bump)     --> Jump(merge)
    //   slowBlock: CallRuntime AllocateInYoung --> Jump(merge)
    //   merge: Phi(fast, slow) --> InitializeXXX --> rest
    struct Fixture {
        NativeAreaAllocator native;
        Chunk chunk {&native};
        Graph graph {&chunk};
        VertexLabelScope labels;
        BB* entry = BB::New(&chunk);
        BB* fastBlock = BB::New(&chunk);
        BB* slowBlock = BB::New(&chunk);
        BB* merge = BB::New(&chunk);
        ValueVertex* glue = graph.GetIntPtrConstant(0);
        ValueVertex* hclass = graph.GetHeapConstant(0, 0);
        ValueVertex* size;
        CanAllocateInYoungVertex* canAllocate;
        AllocateInYoungVertex* fastAllocation;
        CallRuntimeVertex* slowAllocation;
        PhiVertex* phi;
        uint32_t objectSize;
        uint64_t field = JSTaggedValue::Undefined().GetRawData();

        explicit Fixture(uint32_t bytes = JSArray::SIZE + JSTaggedValue::TaggedTypeSize())
            : size(graph.GetInt64Constant(bytes)), objectSize(bytes)
        {
            Allocate();
        }

        void Allocate()
        {
            for (BB* block: {entry, fastBlock, slowBlock, merge}) {
                graph.Add(block);
            }
            canAllocate = New<CanAllocateInYoungVertex>({size});
            Fixture::AddVertex(entry, canAllocate);
            Fixture::SetControl(entry, New<BranchIfTrueVertex>({canAllocate}, fastBlock, slowBlock));
            fastBlock->AddPredecessor(entry);
            slowBlock->AddPredecessor(entry);
            fastAllocation = New<AllocateInYoungVertex>({size});
            Fixture::AddVertex(fastBlock, fastAllocation);
            Fixture::SetControl(fastBlock, New<JumpVertex>({}, merge));
            merge->AddPredecessor(fastBlock);
            slowAllocation = New<CallRuntimeVertex>(
                {graph.GetTaggedConstant(static_cast<uint64_t>(objectSize) | JSTaggedValue::TAG_INT)},
                RTSTUB_ID(AllocateInYoung));
            Fixture::AddVertex(slowBlock, slowAllocation);
            Fixture::SetControl(slowBlock, New<JumpVertex>({}, merge));
            merge->AddPredecessor(slowBlock);
            phi = New<PhiVertex>({fastAllocation, slowAllocation}, VirtualRegister(0));
            Fixture::AddPhi(merge, phi);
            SetControl(merge, New<ReturnVertex>({hclass}));
        }

        explicit Fixture(Initializer kind): Fixture()
        {
            Initialize(kind);
        }

        void SetSlowAllocationArguments(std::initializer_list<ValueVertex*> arguments)
        {
            slowAllocation = New<CallRuntimeVertex>(arguments, RTSTUB_ID(AllocateInYoung));
            slowBlock->GetVertices().clear();
            Fixture::AddVertex(slowBlock, slowAllocation);
            phi->SetInput(1, slowAllocation);
        }

        void AddSlowAllocationHClass()
        {
            SetSlowAllocationArguments({slowAllocation->GetInput(0), hclass});
        }

        template<typename VertexT, typename... Args>
        VertexT* New(std::initializer_list<ValueVertex*> inputs, Args&&... args)
        {
            return Vertex::New<VertexT>(&chunk, inputs, std::forward<Args>(args)...);
        }

        static void AddVertex(BB* block, NonControlVertex* vertex)
        {
            vertex->SetOwner(block);
            block->AddVertex(vertex);
        }

        static void AddPhi(BB* block, PhiVertex* phi)
        {
            // PhiVertex::GetOwner names its virtual register, not its block.
            static_cast<Vertex*>(phi)->SetOwner(block);
            block->AddPhiVertex(phi);
        }

        static void SetControl(BB* block, ControlVertex* control)
        {
            if (control != nullptr) {
                control->SetOwner(block);
                // Match the builder for a new edge, or retain an existing edge's slot.
                if (auto* jump = control->TryCast<UnconditionalControlVertex>()) {
                    uint32_t index = 0;
                    while (index < jump->Target()->PredecessorCount()
                           && jump->Target()->GetPredecessor(index) != block) {
                        ++index;
                    }
                    jump->SetPredecessorId(index);
                }
            }
            block->SetControlVertex(control);
        }

        static void PrependVertex(BB* block, NonControlVertex* vertex)
        {
            vertex->SetOwner(block);
            block->GetVertices().insert(block->GetVertices().begin(), vertex);
        }

        CreateObjectWithBufferInfo Snapshot()
        {
            CreateObjectWithBufferInfo info;
            info.objectSize = objectSize;
            info.fieldCount = 1;
            info.fieldValues = Span<const uint64_t>(&field, 1);
            return info;
        }

        BB* AddSuccessor(BB* block)
        {
            auto* successor = BB::New(&chunk);
            graph.Add(successor);
            Fixture::SetControl(block, New<JumpVertex>({}, successor));
            successor->AddPredecessor(block);
            SetControl(successor, New<ReturnVertex>({hclass}));
            return successor;
        }

        ValueVertex* Initialize(Initializer kind = EMPTY_OBJECT)
        {
            ValueVertex* result = nullptr;
            switch (kind) {
                case EMPTY_OBJECT:
                    result = New<InitializeEmptyObjectVertex>({phi, hclass, size}, objectSize);
                    break;
                case OBJECT_WITH_BUFFER:
                    result = New<InitializeObjectWithBufferVertex>({phi, hclass, hclass}, Snapshot());
                    break;
                case EMPTY_ARRAY:
                    result = New<InitializeEmptyArrayVertex>({phi, hclass, hclass}, objectSize);
                    break;
                case ARRAY_WITH_BUFFER:
                    result = New<InitializeArrayWithBufferVertex>({phi, hclass, hclass}, objectSize);
                    break;
                default:
                    UNREACHABLE();
            }
            Fixture::AddVertex(merge, result);
            return result;
        }

        void BeforeCheck(NonControlVertex* vertex)
        {
            vertex->SetOwner(entry);
            entry->GetVertices().insert(entry->GetVertices().end() - 1, vertex);
        }

        RawI64ToTaggedVertex* EncodeSize(ValueVertex* bytes)
        {
            auto* bits = New<I64BitwiseBinaryVertex>(
                {bytes, graph.GetInt64Constant(static_cast<int64_t>(JSTaggedValue::TAG_INT))},
                IntBitwiseKind::BITWISE_OR);
            auto* tagged = New<RawI64ToTaggedVertex>({bits});
            BeforeCheck(bits);
            BeforeCheck(tagged);
            return tagged;
        }

        void SetSize(ValueVertex* bytes)
        {
            size = bytes;
            canAllocate->SetInput(CanAllocateInYoungVertex::SIZE_INDEX, size);
            fastAllocation->SetInput(AllocateInYoungVertex::SIZE_INDEX, size);
            if (auto* constant = bytes->TryCast<Int64ConstantVertex>()) {
                slowAllocation->SetInput(
                    0,
                    graph.GetTaggedConstant(static_cast<uint64_t>(constant->GetValue()) | JSTaggedValue::TAG_INT));
            } else {
                slowAllocation->SetInput(0, EncodeSize(bytes));
            }
        }

        ValueVertex* ArgumentsSize(ValueVertex* argc)
        {
            auto* count = New<I64SubVertex>({argc, graph.GetInt64Constant(NUM_MANDATORY_JSFUNC_ARGS)});
            auto* dataSize = New<I64ShiftLeftVertex>({count}, TAGGED_TYPE_SIZE_LOG);
            auto* bytes = New<I64AddVertex>({dataSize, graph.GetInt64Constant(TaggedArray::DATA_OFFSET)});
            BeforeCheck(count);
            BeforeCheck(dataSize);
            BeforeCheck(bytes);
            return bytes;
        }

        ActualArgcVertex* ActualArgc()
        {
            auto* argc = New<ActualArgcVertex>({});
            BeforeCheck(argc);
            return argc;
        }

        InitializeUnmappedArgumentsElementsVertex* InitializeElements(ValueVertex* argc)
        {
            auto* result = New<InitializeUnmappedArgumentsElementsVertex>({argc, phi, hclass});
            Fixture::AddVertex(merge, result);
            return result;
        }

        InitializeUnmappedArgumentsVertex* InitializeArguments(ValueVertex* argc)
        {
            // Model allocation/header contracts only. The production lowering
            // fills all elements in a safepoint-free loop before this allocation;
            // JS tests cover that loop and survival across the following GC.
            auto* elements = InitializeElements(argc);
            auto* previous = merge;
            entry = BB::New(&chunk);
            fastBlock = BB::New(&chunk);
            slowBlock = BB::New(&chunk);
            merge = BB::New(&chunk);
            Fixture::SetControl(previous, New<JumpVertex>({}, entry));
            entry->AddPredecessor(previous);
            objectSize = JSArguments::GetInlinedPropertyOffset(JSArguments::LENGTH_OF_INLINE_PROPERTIES);
            size = graph.GetInt64Constant(objectSize);
            Allocate();
            AddSlowAllocationHClass();
            auto* result = New<InitializeUnmappedArgumentsVertex>({argc, phi, elements, hclass, hclass});
            Fixture::AddVertex(merge, result);
            return result;
        }

        void Verify(bool afterRegalloc = false)
        {
            GraphProcessor<ArkSteedGraphVerifier> verifier(&chunk, afterRegalloc);
            verifier.Run(&graph);
        }

        CallRuntimeVertex* Call()
        {
            return New<CallRuntimeVertex>({glue}, RTSTUB_ID(CreateEmptyObject));
        }

        auto* LazyFrame(ValueVertex* value)
        {
            auto* frame = chunk.New<LazyDeoptimizableMixin::LazyDeoptFrameState>(&chunk);
            frame->emplace_back(0, value, DeoptTranslationKind::TAGGED);
            return frame;
        }

        void AddLazyFrame(ValueVertex* value, BB* block)
        {
            auto* call = Call();
            call->SetLazyDeoptFrameState(LazyFrame(value), 0);
            Fixture::AddVertex(block, call);
        }

        void ExpectRejectedInBothPhases(const char* message)
        {
            for (bool afterRegalloc: {false, true}) {
                SCOPED_TRACE(afterRegalloc);
                EXPECT_DEATH(Verify(afterRegalloc), message);
            }
        }

        void ExpectAllocationRecoveryMetadataRejected()
        {
            auto* frame = LazyFrame(hclass);
            EXPECT_DEATH(
                {
                    slowAllocation->SetLazyDeoptFrameState(frame, 0);
                    Verify();
                },
                "Allocation must proceed directly");
            EXPECT_DEATH(
                {
                    slowAllocation->LoadCatchBlock(merge, 0);
                    Verify();
                },
                "Allocation must proceed directly");
            EXPECT_DEATH(
                {
                    slowAllocation->MarkExceptionLazyDeopt();
                    Verify();
                },
                "Allocation must proceed directly");
        }

        void AddEagerFrame(ValueVertex* value)
        {
            auto* deopt = New<DeoptVertex>({}, &chunk, kungfu::DeoptType::NOTINT1, 0);
            EagerDeoptimizableMixin::EagerDeoptFrameState frame(&chunk);
            frame.emplace_back(0, value, DeoptTranslationKind::TAGGED);
            deopt->SetEagerDeoptFrameState(std::move(frame));
            Fixture::SetControl(merge, deopt);
        }

        static AllocatedState Register()
        {
            return AllocatedState(LocationState::REGISTER,
                                  MachineRepresentation::Tagged,
                                  GetAllocatableList<ArkSteedRegister>().First().Code());
        }

        void AddMoves()
        {
            auto slot = AllocatedState(LocationState::STACK_SLOT, MachineRepresentation::Tagged, 0);
            Fixture::AddVertex(merge, New<GapMoveVertex>({}, Register(), slot));
            Fixture::AddVertex(merge, New<GapMoveVertex>({}, slot, Register()));
            Fixture::AddVertex(merge, New<ConstantGapMoveVertex>({}, hclass, Register()));
        }
    };
};

TEST_F(ArkSteedInitializationIntervalVerifierTest, AcceptsAllFourInitializersAndMovesOnlyAfterRegalloc)
{
    for (auto kind: INITIALIZERS) {
        SCOPED_TRACE(kind);
        Fixture f;
        auto* result = f.Initialize(kind);
        Fixture::SetControl(f.merge, f.New<ReturnVertex>({result}));
        f.Verify();
        f.Verify(true);
        f.merge->GetVertices().clear();
        f.AddMoves();
        Fixture::AddVertex(f.merge, result);
        EXPECT_DEATH(f.Verify(), "Illegal operation");
        f.Verify(true);
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsUninitializedAllocationRecoveryMetadata)
{
    for (bool withHClass: {false, true}) {
        SCOPED_TRACE(withHClass);
        Fixture f(EMPTY_OBJECT);
        if (withHClass) {
            f.AddSlowAllocationHClass();
        }
        f.ExpectAllocationRecoveryMetadataRejected();
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsUntaggedRuntimeAllocationSize)
{
    Fixture f(EMPTY_OBJECT);
    f.slowAllocation->SetInput(0, f.size);
    EXPECT_DEATH(f.Verify(), "Allocation requires a tagged size");
    f.slowAllocation->SetInput(0, f.graph.GetTaggedConstant(JSTaggedValue::Undefined().GetRawData()));
    EXPECT_DEATH(f.Verify(), "IsInt");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsOrdinaryRuntimeCallAsUninitializedAllocation)
{
    Fixture f(EMPTY_OBJECT);
    auto* call = f.Call();
    f.slowBlock->GetVertices().clear();
    Fixture::AddVertex(f.slowBlock, call);
    f.phi->SetInput(1, call);
    EXPECT_DEATH(f.Verify(), "Allocation merge Phi requires one fast and one slow allocation");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, AcceptsAllocationWithHClassBeforeInitialization)
{
    for (auto kind: INITIALIZERS) {
        SCOPED_TRACE(kind);
        Fixture f(kind);
        f.AddSlowAllocationHClass();
        EXPECT_TRUE(f.slowAllocation->ReturnsUninitializedAllocation());
        EXPECT_TRUE(IsUninitializedAllocation(f.slowAllocation));
        EXPECT_TRUE(f.slowAllocation->IsCall());
        EXPECT_TRUE(f.slowAllocation->CanAllocate());
        f.Verify();
        f.Verify(true);
        auto* initializer = f.merge->GetVertices().back();
        f.merge->GetVertices().clear();
        f.AddMoves();
        Fixture::AddVertex(f.merge, initializer);
        f.Verify(true);
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsInvalidAllocationArgumentCount)
{
    Fixture f(EMPTY_OBJECT);
    auto* size = f.slowAllocation->GetInput(0);
    f.SetSlowAllocationArguments({});
    EXPECT_TRUE(f.slowAllocation->ReturnsUninitializedAllocation());
    f.ExpectRejectedInBothPhases("Allocation requires size and an optional HClass");
    f.SetSlowAllocationArguments({size, f.hclass, f.hclass});
    EXPECT_TRUE(f.slowAllocation->ReturnsUninitializedAllocation());
    f.ExpectRejectedInBothPhases("Allocation requires size and an optional HClass");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsUntaggedAllocationHClass)
{
    Fixture f(EMPTY_OBJECT);
    f.AddSlowAllocationHClass();
    f.slowAllocation->SetInput(1, f.size);
    f.ExpectRejectedInBothPhases("Allocation requires a tagged HClass");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsInvalidSizeWithHClass)
{
    Fixture f(EMPTY_OBJECT);
    f.AddSlowAllocationHClass();
    f.slowAllocation->SetInput(0, f.size);
    f.ExpectRejectedInBothPhases("Allocation requires a tagged size");
    f.slowAllocation->SetInput(0, f.graph.GetTaggedConstant(JSTaggedValue::Undefined().GetRawData()));
    f.ExpectRejectedInBothPhases("IsInt");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, AcceptsCompletedValuesInFramesAndPhi)
{
    {
        Fixture f;
        auto* result = f.Initialize();
        f.AddLazyFrame(result, f.merge);
        f.AddEagerFrame(result);
        f.Verify();
    }
    Fixture f;
    auto* result = f.Initialize();
    auto* exit = f.AddSuccessor(f.merge);
    auto* merged = f.New<PhiVertex>({result}, VirtualRegister(0));
    Fixture::AddPhi(exit, merged);
    Fixture::SetControl(exit, f.New<ReturnVertex>({merged}));
    f.Verify();
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsReturnEscapeAfterInitialization)
{
    Fixture f(EMPTY_OBJECT);
    Fixture::SetControl(f.merge, f.New<ReturnVertex>({f.fastAllocation}));
    EXPECT_DEATH(f.Verify(), "Allocation escapes");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsFieldStoreEscape)
{
    Fixture f(EMPTY_OBJECT);
    Fixture::AddVertex(f.merge, f.New<StoreTaggedFieldVertex>({f.hclass, f.slowAllocation}, JSObject::ELEMENTS_OFFSET));
    EXPECT_DEATH(f.Verify(), "Allocation escapes");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsPhiEscape)
{
    Fixture f(EMPTY_OBJECT);
    auto* exit = f.AddSuccessor(f.merge);
    Fixture::AddPhi(exit, f.New<PhiVertex>({f.fastAllocation}, VirtualRegister(0)));
    EXPECT_DEATH(f.Verify(), "Allocation merge Phi requires");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsFrameEscapeWithoutInputUse)
{
    for (bool withHClass: {false, true}) {
        SCOPED_TRACE(withHClass);
        Fixture f(EMPTY_OBJECT);
        if (withHClass) {
            f.AddSlowAllocationHClass();
        }
        EXPECT_DEATH(
            {
                f.AddLazyFrame(f.slowAllocation, f.merge);
                f.Verify();
            },
            "Allocation escapes into frame state");
        f.AddEagerFrame(f.slowAllocation);
        f.ExpectRejectedInBothPhases("Allocation escapes into frame state");
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsFrameEscapeInEarlierTraversalBlock)
{
    Fixture f;
    for (ValueVertex* value: {static_cast<ValueVertex*>(f.fastAllocation), static_cast<ValueVertex*>(f.phi)}) {
        SCOPED_TRACE(value == f.phi ? "Merge Phi" : "Raw allocation");
        if (value == f.phi) {
            f.Initialize();
        }
        f.AddLazyFrame(value, f.entry);
        EXPECT_DEATH(f.Verify(), "Allocation escapes into frame state");
        f.entry->GetVertices().pop_back();
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsMismatchedSlowAllocationSize)
{
    for (auto kind: INITIALIZERS) {
        Fixture f;
        f.Initialize(kind);
        f.slowAllocation->SetInput(
            0, f.graph.GetTaggedConstant(JSTaggedValue(static_cast<int32_t>(f.objectSize + 8)).GetRawData()));
        EXPECT_DEATH(f.Verify(), "");
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsUnprotectedInitializerRoots)
{
    for (auto kind: INITIALIZERS) {
        Fixture f;
        auto* init = f.Initialize(kind);
        // HClass, GlobalEnv and literal roots all occupy the second input.
        auto* unprotected = f.New<InitialValueVertex>({}, 0);
        Fixture::PrependVertex(f.entry, unprotected);
        init->SetInput(1, unprotected);
        f.ExpectRejectedInBothPhases("Initializer requires a rooted constant");
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsAllocationInOtherInitializerFields)
{
    Fixture f;
    auto* init = f.Initialize(ARRAY_WITH_BUFFER);
    init->SetInput(InitializeArrayWithBufferVertex::LITERAL_INDEX, f.fastAllocation);
    EXPECT_DEATH(f.Verify(), "Allocation escapes");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsWrongInputRepresentations)
{
    for (auto kind: INITIALIZERS) {
        Fixture f;
        auto* init = f.Initialize(kind);
        init->SetInput(InitializeEmptyObjectVertex::HCLASS_INDEX, f.graph.GetIntPtrConstant(0));
        EXPECT_DEATH(f.Verify(), "GetValueRepresentation\\(\\) == expectedRepr");
        init->SetInput(InitializeEmptyObjectVertex::HCLASS_INDEX, f.hclass);
        f.fastAllocation->SetInput(AllocateInYoungVertex::SIZE_INDEX, f.hclass);
        EXPECT_DEATH(f.Verify(), "GetValueRepresentation\\(\\) == expectedRepr");
    }
    Fixture f;
    auto* init = f.Initialize(ARRAY_WITH_BUFFER);
    init->SetInput(InitializeArrayWithBufferVertex::LITERAL_INDEX, f.size);
    EXPECT_DEATH(f.Verify(), "GetValueRepresentation\\(\\) == expectedRepr");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsCallsAndSecondAllocation)
{
    for (bool withHClass: {false, true}) {
        SCOPED_TRACE(withHClass);
        Fixture f;
        if (withHClass) {
            f.AddSlowAllocationHClass();
        }
        Fixture::AddVertex(f.merge, f.Call());
        f.Initialize();
        f.ExpectRejectedInBothPhases("Illegal operation");
        f.merge->GetVertices().erase(f.merge->GetVertices().begin());
        Fixture::AddVertex(f.slowBlock, f.Call());
        f.ExpectRejectedInBothPhases("Illegal operation");
        f.slowBlock->GetVertices().pop_back();
        Fixture::AddVertex(f.fastBlock, f.New<AllocateInYoungVertex>({f.size}));
        EXPECT_DEATH(f.Verify(), "Second allocation");
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsOrdinaryLoadsEvenAfterRegalloc)
{
    Fixture f;
    Fixture::AddVertex(f.merge, f.New<LoadTaggedFieldVertex>({f.hclass}, TaggedObject::HCLASS_OFFSET));
    f.Initialize();
    EXPECT_DEATH(f.Verify(true), "Illegal operation");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsDeoptAndBranchInsideInterval)
{
    for (bool withHClass: {false, true}) {
        SCOPED_TRACE(withHClass);
        Fixture f;
        if (withHClass) {
            f.AddSlowAllocationHClass();
        }
        f.AddEagerFrame(f.hclass);
        f.ExpectRejectedInBothPhases("Illegal operation");
        f.AddSuccessor(f.merge);
        f.ExpectRejectedInBothPhases("Illegal operation");
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsIncompleteAndCrossBlockIntervals)
{
    {
        Fixture f;
        EXPECT_DEATH(f.Verify(), "");
    }
    Fixture g;
    auto* init = g.Initialize();
    g.merge->GetVertices().pop_back();
    auto* later = g.AddSuccessor(g.merge);
    Fixture::AddVertex(later, init);
    EXPECT_DEATH(g.Verify(), "");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsAllocationBlockWithoutJumpToMerge)
{
    {
        Fixture f;
        Fixture::SetControl(f.fastBlock, nullptr);
        EXPECT_DEATH(f.Verify(), "");
    }
    Fixture g;
    auto* stray = BB::New(&g.chunk);
    g.graph.Add(stray);
    Fixture::SetControl(stray, g.New<ReturnVertex>({g.hclass}));
    Fixture::SetControl(g.slowBlock, g.New<JumpVertex>({}, stray));
    stray->AddPredecessor(g.slowBlock);
    EXPECT_DEATH(g.Verify(), "Allocation must jump to its merge Phi block");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsInitializerBeforeAllocationAndDuplicateInitializer)
{
    Fixture f;
    auto* init = f.Initialize();
    f.merge->GetVertices().pop_back();
    Fixture::AddVertex(f.fastBlock, init);
    EXPECT_DEATH(f.Verify(), "Allocation Phi may only feed its matching initializer");
    f.fastBlock->GetVertices().pop_back();
    Fixture::AddVertex(f.merge, init);
    f.Initialize();
    EXPECT_DEATH(f.Verify(), "Allocation Phi may only feed its matching initializer");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsDifferentAllocationAndNonAllocation)
{
    Fixture f;
    auto* init = f.Initialize();
    init->SetInput(InitializeEmptyObjectVertex::ALLOCATION_INDEX, f.fastAllocation);
    EXPECT_DEATH(f.Verify(), "Allocation escapes");
    init->SetInput(InitializeEmptyObjectVertex::ALLOCATION_INDEX, f.hclass);
    EXPECT_DEATH(f.Verify(), "");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsRepeatedScheduling)
{
    Fixture f(EMPTY_OBJECT);
    Fixture::AddVertex(f.merge, f.fastAllocation);
    EXPECT_DEATH(f.Verify(), "");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsAllocationAsConstantMoveSource)
{
    Fixture f;
    Fixture::AddVertex(f.merge, f.New<ConstantGapMoveVertex>({}, f.fastAllocation, Fixture::Register()));
    f.Initialize();
    EXPECT_DEATH(f.Verify(true), "");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsInvalidStaticSizes)
{
    for (uint32_t size: {0U,
                         static_cast<uint32_t>(JSObject::SIZE - 1),
                         static_cast<uint32_t>(JSObject::SIZE + 1),
                         static_cast<uint32_t>(MAX_REGULAR_HEAP_OBJECT_SIZE + JSTaggedValue::TaggedTypeSize()),
                         UINT32_MAX}) {
        Fixture f(size);
        f.Initialize();
        EXPECT_DEATH(f.Verify(), "");
    }
    for (auto kind: {EMPTY_ARRAY, ARRAY_WITH_BUFFER}) {
        Fixture f(JSArray::SIZE);
        f.Initialize(kind);
        EXPECT_DEATH(f.Verify(), "");
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsStaticInitializerSizeMismatch)
{
    for (auto kind: INITIALIZERS) {
        Fixture f;
        f.objectSize += JSTaggedValue::TaggedTypeSize();
        f.Initialize(kind);
        EXPECT_DEATH(f.Verify(), "");
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsInvalidSnapshotAtConstruction)
{
    Fixture f;
    auto info = f.Snapshot();
    info.fieldCount = 2;
    EXPECT_DEATH(f.New<InitializeObjectWithBufferVertex>({f.phi, f.hclass, f.hclass}, info),
                 "fieldValues.size");
    info.fieldCount = 1;
    info.fieldValues = Span<const uint64_t>(nullptr, 1);
    EXPECT_DEATH(f.New<InitializeObjectWithBufferVertex>({f.phi, f.hclass, f.hclass}, info),
                 "fieldValues.data");
    info.fieldValues = Span<const uint64_t>(&f.field, 1);
    info.objectSize = JSObject::SIZE;
    EXPECT_DEATH(f.New<InitializeObjectWithBufferVertex>({f.phi, f.hclass, f.hclass}, info),
                 "fieldCount <=");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsWrongFixedInputCountAtConstruction)
{
    Fixture f;
    EXPECT_DEATH(f.New<AllocateInYoungVertex>({}), "GetInputCount");
    EXPECT_DEATH(f.New<InitializeEmptyObjectVertex>({f.phi, f.hclass}, f.objectSize), "GetInputCount");
    auto info = f.Snapshot();
    EXPECT_DEATH(f.New<InitializeObjectWithBufferVertex>({f.phi, f.hclass}, info), "GetInputCount");
    EXPECT_DEATH(f.New<InitializeEmptyArrayVertex>({f.phi, f.hclass}, f.objectSize), "GetInputCount");
    EXPECT_DEATH(f.New<InitializeArrayWithBufferVertex>({f.phi, f.hclass}, f.objectSize), "GetInputCount");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsDynamicLiteralAllocationSize)
{
    for (auto kind: INITIALIZERS) {
        Fixture f;
        auto* argc = f.ActualArgc();
        f.SetSize(f.ArgumentsSize(argc));
        f.Initialize(kind);
        f.ExpectRejectedInBothPhases("Literal allocation requires its static size");
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, AcceptsDynamicArgumentsAndNonGCInitializationBarriers)
{
    for (bool object: {false, true}) {
        Fixture f;
        f.AddSlowAllocationHClass();
        auto* argc = f.ActualArgc();
        f.SetSize(f.ArgumentsSize(argc));
        auto* elementsAllocation = f.slowAllocation;
        ValueVertex* result = object ? static_cast<ValueVertex*>(f.InitializeArguments(argc))
                                     : static_cast<ValueVertex*>(f.InitializeElements(argc));
        ASSERT_EQ(elementsAllocation->GetInputCount(), 2U);
        EXPECT_EQ(elementsAllocation->GetInput(1), f.hclass);
        ASSERT_EQ(f.slowAllocation->GetInputCount(), 2U);
        EXPECT_EQ(f.slowAllocation->GetInput(1), f.hclass);
        if (object) {
            EXPECT_NE(elementsAllocation, f.slowAllocation);
        }
        Fixture::SetControl(f.merge, f.New<ReturnVertex>({result}));
        f.Verify();
        f.Verify(true);
        f.merge->GetVertices().clear();
        f.AddMoves();
        Fixture::AddVertex(f.merge, result);
        EXPECT_DEATH(f.Verify(), "Illegal operation");
        f.Verify(true);
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, AcceptsConstantArgumentsSizesIncludingHugeAndFullWidth)
{
    for (int64_t count: {int64_t {0},
                         int64_t {1},
                         // 8: divisor keeping the size within the regular-object limit
                         static_cast<int64_t>(MAX_REGULAR_HEAP_OBJECT_SIZE / 8),
                         int64_t {1} << 29,
                         static_cast<int64_t>(INT32_MAX)}) {
        SCOPED_TRACE(count);
        Fixture f;
        f.AddSlowAllocationHClass();
        auto* argc = f.graph.GetInt64Constant(count + NUM_MANDATORY_JSFUNC_ARGS);
        int64_t bytes = count * JSTaggedValue::TaggedTypeSize() + TaggedArray::DATA_OFFSET;
        f.SetSize(f.graph.GetInt64Constant(bytes));
        f.InitializeElements(argc);
        EXPECT_EQ(
            JSTaggedValue(f.slowAllocation->GetInput(0)->Cast<TaggedConstantVertex>()->GetValue()).GetLargeUInt(),
            static_cast<uint64_t>(bytes));
        f.Verify();
        f.Verify(true);

        auto* tagged = f.EncodeSize(f.size);
        f.slowAllocation->SetInput(0, tagged);
        f.Verify();
        tagged->SetInput(
            0,
            f.graph.GetInt64Constant(static_cast<int64_t>(static_cast<uint64_t>(bytes) | JSTaggedValue::TAG_INT)));
        f.Verify();
        f.Verify(true);
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsFullWidthSizeTruncation)
{
    Fixture f;
    constexpr int64_t count = int64_t {1} << 29;
    constexpr int64_t bytes = count * JSTaggedValue::TaggedTypeSize() + TaggedArray::DATA_OFFSET;
    f.SetSize(f.graph.GetInt64Constant(bytes));
    f.InitializeElements(f.graph.GetInt64Constant(count + NUM_MANDATORY_JSFUNC_ARGS));
    f.slowAllocation->SetInput(0, f.graph.GetTaggedConstant(static_cast<uint32_t>(bytes) | JSTaggedValue::TAG_INT));
    f.ExpectRejectedInBothPhases("Allocation must match the checked size");
    f.SetSize(f.graph.GetInt64Constant(static_cast<uint32_t>(bytes)));
    f.ExpectRejectedInBothPhases("Arguments elements size must match the count");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsNarrowedDynamicSlowSize)
{
    Fixture f;
    auto* argc = f.ActualArgc();
    f.SetSize(f.ArgumentsSize(argc));
    f.InitializeElements(argc);
    auto* narrowed = f.New<I64BitwiseBinaryVertex>({f.size, f.graph.GetInt64Constant(UINT32_MAX)},
                                                   IntBitwiseKind::BITWISE_AND);
    f.BeforeCheck(narrowed);
    f.slowAllocation->SetInput(0, f.EncodeSize(narrowed));
    f.ExpectRejectedInBothPhases("Allocation must match the checked size");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsMismatchedDynamicSlowSizeAndTag)
{
    Fixture f;
    auto* argc = f.ActualArgc();
    f.SetSize(f.ArgumentsSize(argc));
    f.InitializeElements(argc);
    auto* tagged = f.slowAllocation->GetInput(0)->Cast<RawI64ToTaggedVertex>();
    auto* encoding = tagged->GetInput(0)->Cast<I64BitwiseBinaryVertex>();
    encoding->SetInput(I64BitwiseBinaryVertex::LEFT_INDEX, argc);
    f.ExpectRejectedInBothPhases("Allocation must match the checked size");
    encoding->SetInput(I64BitwiseBinaryVertex::LEFT_INDEX, f.size);
    encoding->SetInput(I64BitwiseBinaryVertex::RIGHT_INDEX, f.graph.GetInt64Constant(0));
    f.ExpectRejectedInBothPhases("IsInt64Constant");
    auto* unencoded = f.New<InitialValueVertex>({}, 0);
    Fixture::PrependVertex(f.entry, unencoded);
    f.slowAllocation->SetInput(0, unencoded);
    f.ExpectRejectedInBothPhases("Is<T>");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsMismatchedArgumentsInitializationCount)
{
    for (bool object: {false, true}) {
        Fixture f;
        auto* argc = f.ActualArgc();
        f.SetSize(f.ArgumentsSize(argc));
        if (object) {
            auto* init = f.InitializeArguments(argc);
            init->SetInput(InitializeUnmappedArgumentsVertex::NUM_ARGS_INDEX, f.ActualArgc());
            f.ExpectRejectedInBothPhases("Arguments object and elements must use the same count");
        } else {
            auto* init = f.InitializeElements(argc);
            init->SetInput(InitializeUnmappedArgumentsElementsVertex::NUM_ARGS_INDEX, f.ActualArgc());
            f.ExpectRejectedInBothPhases("Arguments elements size must match the count");
        }
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsWrongArgumentsSizeFormula)
{
    enum class SizeFormulaPart { ARGUMENT_COUNT, ELEMENT_SIZE, HEADER_SIZE };
    for (auto part:
         {SizeFormulaPart::ARGUMENT_COUNT, SizeFormulaPart::ELEMENT_SIZE, SizeFormulaPart::HEADER_SIZE}) {
        SCOPED_TRACE(static_cast<int>(part));
        Fixture f;
        auto* argc = f.ActualArgc();
        auto* count
            = f.New<I64SubVertex>({argc,
                                   f.graph.GetInt64Constant(NUM_MANDATORY_JSFUNC_ARGS
                                                            + (part == SizeFormulaPart::ARGUMENT_COUNT ? 1 : 0))});
        auto* shift = f.New<I64ShiftLeftVertex>(
            {count}, TAGGED_TYPE_SIZE_LOG + (part == SizeFormulaPart::ELEMENT_SIZE ? 1 : 0));
        auto* size = f.New<I64AddVertex>(
            {shift,
             // 8: deliberate header-size error in the corrupted formula
             f.graph.GetInt64Constant(TaggedArray::DATA_OFFSET + (part == SizeFormulaPart::HEADER_SIZE ? 8 : 0))});
        f.BeforeCheck(count);
        f.BeforeCheck(shift);
        f.BeforeCheck(size);
        f.SetSize(size);
        f.InitializeElements(argc);
        f.ExpectRejectedInBothPhases("");
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsInvalidConstantArgumentsCount)
{
    for (int64_t argc: {int64_t {NUM_MANDATORY_JSFUNC_ARGS - 1},
                        static_cast<int64_t>(INT32_MAX) + NUM_MANDATORY_JSFUNC_ARGS + 1}) {
        Fixture f;
        f.SetSize(f.graph.GetInt64Constant(TaggedArray::DATA_OFFSET));
        f.InitializeElements(f.graph.GetInt64Constant(argc));
        f.ExpectRejectedInBothPhases("");
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsWrongArgumentsObjectSize)
{
    Fixture f;
    auto* argc = f.ActualArgc();
    f.SetSize(f.ArgumentsSize(argc));
    f.InitializeArguments(argc);
    f.SetSize(f.graph.GetInt64Constant(f.objectSize + JSTaggedValue::TaggedTypeSize()));
    f.ExpectRejectedInBothPhases("Arguments object requires its static layout size");
    f.SetSize(f.ArgumentsSize(argc));
    f.ExpectRejectedInBothPhases("Arguments object requires its static layout size");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsUnsafeOperationsDuringArgumentsInitialization)
{
    for (bool object: {false, true}) {
        Fixture f;
        auto* argc = f.ActualArgc();
        f.SetSize(f.ArgumentsSize(argc));
        ValueVertex* init = object ? static_cast<ValueVertex*>(f.InitializeArguments(argc))
                                   : static_cast<ValueVertex*>(f.InitializeElements(argc));
        for (NonControlVertex* operation:
             {static_cast<NonControlVertex*>(f.Call()),
              static_cast<NonControlVertex*>(f.New<LoadTaggedFieldVertex>({f.hclass}, TaggedObject::HCLASS_OFFSET)),
              static_cast<NonControlVertex*>(f.New<I64AddVertex>({f.size, f.size}))}) {
            f.merge->GetVertices().clear();
            Fixture::AddVertex(f.merge, operation);
            Fixture::AddVertex(f.merge, init);
            f.ExpectRejectedInBothPhases("Illegal operation");
        }
        f.merge->GetVertices().clear();
        Fixture::AddVertex(f.merge, init);
        f.AddLazyFrame(f.phi, f.merge);
        f.ExpectRejectedInBothPhases("Allocation escapes into frame state");
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsSizeConversionBetweenCheckAndAllocation)
{
    Fixture f;
    auto* argc = f.ActualArgc();
    f.SetSize(f.ArgumentsSize(argc));
    f.InitializeElements(argc);
    auto* tagged = f.slowAllocation->GetInput(0)->Cast<RawI64ToTaggedVertex>();
    auto* bits = tagged->GetInput(0)->Cast<I64BitwiseBinaryVertex>();
    auto& scheduled = f.entry->GetVertices();
    scheduled.erase(std::find(scheduled.begin(), scheduled.end(), tagged));
    scheduled.erase(std::find(scheduled.begin(), scheduled.end(), bits));
    f.slowBlock->GetVertices().clear();
    Fixture::AddVertex(f.slowBlock, bits);
    Fixture::AddVertex(f.slowBlock, tagged);
    Fixture::AddVertex(f.slowBlock, f.slowAllocation);
    f.ExpectRejectedInBothPhases("Only register moves may separate the check and allocation");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsDynamicAllocationRecoveryMetadata)
{
    Fixture f;
    f.AddSlowAllocationHClass();
    auto* argc = f.ActualArgc();
    f.SetSize(f.ArgumentsSize(argc));
    f.InitializeElements(argc);
    f.ExpectAllocationRecoveryMetadataRejected();
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsSecondMergePhiInInterval)
{
    Fixture f;
    auto* second = f.New<PhiVertex>({f.fastAllocation, f.slowAllocation}, VirtualRegister(1));
    Fixture::AddPhi(f.merge, second);
    f.Initialize();
    EXPECT_DEATH(f.Verify(), "Second allocation");
}
TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsMissingCapacityCheck)
{
    Fixture f(EMPTY_OBJECT);
    Fixture::SetControl(f.entry, f.New<JumpVertex>({}, f.fastBlock));
    EXPECT_DEATH(f.Verify(), "Is<T>");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, CapacityPredicateHasReadOnlyTransientValueConstraints)
{
    Fixture f;
    auto* check = f.canAllocate;
    check->SetRegallocInfo(
        f.chunk.New<RegallocValueVertexInfo>(&f.chunk, check->GetInputCount(), check->GetMachineRepresentation()));
    check->SetValueLocationConstraints();
    using Policy = UnallocatedState::ExtendedPolicy;
    EXPECT_EQ(UnallocatedState::Cast(check->Result().GetOperand()).GetExtendedPolicy(), Policy::MUST_HAVE_REGISTER);
    for (uint32_t i = 0; i < check->GetInputCount(); ++i) {
        auto input = UnallocatedState::Cast(check->GetInputLocation(i)->GetOperand());
        EXPECT_EQ(input.GetExtendedPolicy(), Policy::MUST_HAVE_REGISTER);
        EXPECT_FALSE(input.IsUsedAtStart());
    }
    EXPECT_EQ(check->GetValueRepresentation(), ValueRepresentation::TAGGED);
    EXPECT_EQ(CanAllocateInYoungVertex::INPUT_TYPES[CanAllocateInYoungVertex::SIZE_INDEX],
              ValueRepresentation::INT64);
    EXPECT_EQ(check->GetNumTemporariesNeeded<ArkSteedRegister>(), 2U);
    EXPECT_TRUE(check->CanRead());
    EXPECT_TRUE(check->IsNotIdempotent());
    EXPECT_FALSE(CanParticipateInCSE(CanAllocateInYoungVertex::PROPERTIES));
    EXPECT_FALSE(check->CanWrite());
    EXPECT_FALSE(check->CanAllocate());
    EXPECT_FALSE(check->IsCall());
    EXPECT_FALSE(check->IsDeferredCall());
    EXPECT_FALSE(check->CanThrow());
    EXPECT_FALSE(check->CanEagerDeopt());
    EXPECT_FALSE(check->CanLazyDeopt());
    f.Initialize();
    check->SetRegallocInfo(nullptr);
    f.Verify();
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsUnrelatedBranchCondition)
{
    Fixture f(EMPTY_OBJECT);
    f.entry->GetControlVertex()->SetInput(0, f.graph.GetTaggedConstant(JSTaggedValue::True().GetRawData()));
    EXPECT_DEATH(f.Verify(), "Allocation requires a capacity predicate");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsUnscheduledOrEarlierBlockPredicate)
{
    Fixture f(EMPTY_OBJECT);
    f.entry->GetVertices().clear();
    EXPECT_DEATH(f.Verify(), "");
    auto* earlier = BB::New(&f.chunk);
    Fixture::AddVertex(earlier, f.canAllocate);
    Fixture::SetControl(earlier, f.New<JumpVertex>({}, f.entry));
    f.entry->AddPredecessor(earlier);
    f.graph.Add(earlier);
    EXPECT_DEATH(f.Verify(), "Capacity predicate must be scheduled in the checking block");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsOperationsBetweenPredicateAndBranch)
{
    Fixture f(EMPTY_OBJECT);
    for (NonControlVertex* operation:
         {static_cast<NonControlVertex*>(f.Call()),
          static_cast<NonControlVertex*>(f.New<StoreI64ToAddressVertex>({f.glue, f.glue}, 0)),
          static_cast<NonControlVertex*>(f.New<I64AddVertex>({f.size, f.size}))}) {
        Fixture::AddVertex(f.entry, operation);
        f.ExpectRejectedInBothPhases("Only register moves may separate the capacity predicate and branch");
        f.entry->GetVertices().pop_back();
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, AcceptsEffectsBeforePredicateAndMovesAfterRegalloc)
{
    Fixture f(EMPTY_OBJECT);
    Fixture::PrependVertex(f.entry, f.Call());
    f.Verify();
    auto slot = AllocatedState(LocationState::STACK_SLOT, MachineRepresentation::Tagged, 0);
    Fixture::AddVertex(f.entry, f.New<GapMoveVertex>({}, Fixture::Register(), slot));
    Fixture::AddVertex(f.entry, f.New<ConstantGapMoveVertex>({}, f.hclass, Fixture::Register()));
    Fixture::PrependVertex(f.fastBlock, f.New<GapMoveVertex>({}, slot, Fixture::Register()));
    EXPECT_DEATH(f.Verify(), "Only register moves may separate the capacity predicate and branch");
    f.Verify(true);
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsRepeatedPredicateScheduling)
{
    Fixture f(EMPTY_OBJECT);
    Fixture::AddVertex(f.entry, f.canAllocate);
    EXPECT_DEATH(f.Verify(), "");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsChangedSizeAfterCheck)
{
    Fixture f(EMPTY_OBJECT);
    // 8: deliberate size mismatch
    f.canAllocate->SetInput(CanAllocateInYoungVertex::SIZE_INDEX, f.graph.GetInt64Constant(f.objectSize + 8));
    EXPECT_DEATH(f.Verify(), "Allocation must match the checked size");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsReversedCapacityEdges)
{
    Fixture f(EMPTY_OBJECT);
    Fixture::SetControl(f.entry, f.New<BranchIfTrueVertex>({f.canAllocate}, f.slowBlock, f.fastBlock));
    EXPECT_DEATH(f.Verify(), "Fast allocation requires the successful check edge");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsCallsBetweenCheckAndAllocation)
{
    Fixture f(EMPTY_OBJECT);
    Fixture::PrependVertex(f.fastBlock, f.Call());
    f.ExpectRejectedInBothPhases("Only register moves may separate the check and allocation");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsPhiPredecessorMismatch)
{
    Fixture f(EMPTY_OBJECT);
    f.phi->SetInput(0, f.slowAllocation);
    f.phi->SetInput(1, f.fastAllocation);
    EXPECT_DEATH(f.Verify(), "Allocation Phi input must match its predecessor");
    std::swap(f.merge->GetPredecessors()[0], f.merge->GetPredecessors()[1]);
    f.fastBlock->GetControlVertex()->Cast<JumpVertex>()->SetPredecessorId(1);
    f.slowBlock->GetControlVertex()->Cast<JumpVertex>()->SetPredecessorId(0);
    f.Verify();
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsAllocationWithoutMergePhi)
{
    Fixture f;
    f.merge->GetPhis().clear();
    EXPECT_DEATH(f.Verify(), "Allocation requires a merge and initializer");
}
#else
TEST(ArkSteedInitializationIntervalVerifierTest, RequiresDebugAssertions)
{
    GTEST_SKIP() << "Initialization interval verification is debug-only";
}
#endif

} // namespace panda::ecmascript::arksteed
