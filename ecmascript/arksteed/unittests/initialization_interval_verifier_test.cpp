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
            : size(graph.GetInt32Constant(static_cast<int32_t>(bytes))), objectSize(bytes)
        {
            for (BB* block: {entry, fastBlock, slowBlock, merge}) {
                graph.Add(block);
            }
            canAllocate = New<CanAllocateInYoungVertex>({glue, size});
            entry->AddVertex(canAllocate);
            entry->SetControlVertex(New<BranchIfTrueVertex>({canAllocate}, fastBlock, slowBlock));
            fastBlock->AddPredecessor(entry);
            slowBlock->AddPredecessor(entry);
            fastAllocation = New<AllocateInYoungVertex>({glue, size});
            fastBlock->AddVertex(fastAllocation);
            fastBlock->SetControlVertex(New<JumpVertex>({}, merge));
            merge->AddPredecessor(fastBlock);
            slowAllocation = New<CallRuntimeVertex>(
                {graph.GetTaggedConstant(JSTaggedValue(static_cast<int32_t>(bytes)).GetRawData())},
                RTSTUB_ID(AllocateInYoung));
            slowBlock->AddVertex(slowAllocation);
            slowBlock->SetControlVertex(New<JumpVertex>({}, merge));
            merge->AddPredecessor(slowBlock);
            phi = New<PhiVertex>({fastAllocation, slowAllocation}, VirtualRegister(0));
            merge->AddPhiVertex(phi);
        }

        explicit Fixture(Initializer kind): Fixture()
        {
            Initialize(kind);
        }

        template<typename VertexT, typename... Args>
        VertexT* New(std::initializer_list<ValueVertex*> inputs, Args&&... args)
        {
            return Vertex::New<VertexT>(&chunk, inputs, std::forward<Args>(args)...);
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
            block->SetControlVertex(New<JumpVertex>({}, successor));
            successor->AddPredecessor(block);
            return successor;
        }

        ValueVertex* Initialize(Initializer kind = EMPTY_OBJECT)
        {
            ValueVertex* result = nullptr;
            switch (kind) {
                case EMPTY_OBJECT:
                    result = New<InitializeEmptyObjectVertex>({phi, glue, hclass, size}, objectSize);
                    break;
                case OBJECT_WITH_BUFFER:
                    result = New<InitializeObjectWithBufferVertex>({phi, glue, hclass}, Snapshot());
                    break;
                case EMPTY_ARRAY:
                    result = New<InitializeEmptyArrayVertex>({phi, glue, hclass}, objectSize);
                    break;
                case ARRAY_WITH_BUFFER:
                    result = New<InitializeArrayWithBufferVertex>({phi, glue, hclass}, objectSize);
                    break;
                default:
                    UNREACHABLE();
            }
            merge->AddVertex(result);
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
            block->AddVertex(call);
        }

        void ExpectRejectedInBothPhases(const char* message)
        {
            for (bool afterRegalloc: {false, true}) {
                SCOPED_TRACE(afterRegalloc);
                EXPECT_DEATH(Verify(afterRegalloc), message);
            }
        }

        void AddEagerFrame(ValueVertex* value)
        {
            auto* deopt = New<DeoptVertex>({}, &chunk, kungfu::DeoptType::NOTINT1, 0);
            EagerDeoptimizableMixin::EagerDeoptFrameState frame(&chunk);
            frame.emplace_back(0, value, DeoptTranslationKind::TAGGED);
            deopt->SetEagerDeoptFrameState(std::move(frame));
            merge->SetControlVertex(deopt);
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
            merge->AddVertex(New<GapMoveVertex>({}, Register(), slot));
            merge->AddVertex(New<GapMoveVertex>({}, slot, Register()));
            merge->AddVertex(New<ConstantGapMoveVertex>({}, hclass, Register()));
        }
    };
};

TEST_F(ArkSteedInitializationIntervalVerifierTest, AcceptsAllFourInitializersAndMovesOnlyAfterRegalloc)
{
    for (auto kind: INITIALIZERS) {
        SCOPED_TRACE(kind);
        Fixture f;
        auto* result = f.Initialize(kind);
        f.merge->SetControlVertex(f.New<ReturnVertex>({result}));
        f.Verify();
        f.Verify(true);
        f.merge->GetVertices().clear();
        f.AddMoves();
        f.merge->AddVertex(result);
        EXPECT_DEATH(f.Verify(), "Illegal operation");
        f.Verify(true);
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsRawAllocationRecoveryMetadata)
{
    Fixture f(EMPTY_OBJECT);
    auto* frame = f.LazyFrame(f.hclass);
    EXPECT_DEATH(
        {
            f.slowAllocation->SetLazyDeoptFrameState(frame, 0);
            f.Verify();
        },
        "Raw allocation must proceed directly");
    EXPECT_DEATH(
        {
            f.slowAllocation->LoadCatchBlock(f.merge, 0);
            f.Verify();
        },
        "Raw allocation must proceed directly");
    EXPECT_DEATH(
        {
            f.slowAllocation->MarkExceptionLazyDeopt();
            f.Verify();
        },
        "Raw allocation must proceed directly");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsUntaggedRuntimeAllocationSize)
{
    Fixture f(EMPTY_OBJECT);
    f.slowAllocation->SetInput(0, f.size);
    EXPECT_DEATH(f.Verify(), "Raw allocation requires a tagged constant size");
    f.slowAllocation->SetInput(0, f.graph.GetTaggedConstant(JSTaggedValue::Undefined().GetRawData()));
    EXPECT_DEATH(f.Verify(), "IsInt");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsOrdinaryRuntimeCallAsRawAllocation)
{
    Fixture f(EMPTY_OBJECT);
    auto* call = f.Call();
    f.slowBlock->GetVertices().clear();
    f.slowBlock->AddVertex(call);
    f.phi->SetInput(1, call);
    EXPECT_DEATH(f.Verify(), "Allocation merge Phi requires one fast and one slow allocation");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, DistinguishesInitializedRuntimeOverload)
{
    Fixture f;
    auto* call = f.New<CallRuntimeVertex>({f.slowAllocation->GetInput(0), f.hclass}, RTSTUB_ID(AllocateInYoung));
    EXPECT_FALSE(call->ReturnsRawAllocation());
    EXPECT_FALSE(IsRawAllocation(call));
    auto* frame = f.LazyFrame(f.hclass);
    call->SetLazyDeoptFrameState(frame, 0);
    call->VerifyInputs();
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
    exit->AddPhiVertex(merged);
    exit->SetControlVertex(f.New<ReturnVertex>({merged}));
    f.Verify();
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsReturnEscapeAfterInitialization)
{
    Fixture f(EMPTY_OBJECT);
    f.merge->SetControlVertex(f.New<ReturnVertex>({f.fastAllocation}));
    EXPECT_DEATH(f.Verify(), "Allocation escapes");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsFieldStoreEscape)
{
    Fixture f(EMPTY_OBJECT);
    f.merge->AddVertex(f.New<StoreTaggedFieldVertex>({f.hclass, f.slowAllocation}, JSObject::ELEMENTS_OFFSET));
    EXPECT_DEATH(f.Verify(), "Allocation escapes");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsPhiEscape)
{
    Fixture f(EMPTY_OBJECT);
    auto* exit = f.AddSuccessor(f.merge);
    exit->AddPhiVertex(f.New<PhiVertex>({f.fastAllocation, f.fastAllocation}, VirtualRegister(0)));
    EXPECT_DEATH(f.Verify(), "Allocation merge Phi requires");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsFrameEscapeWithoutInputUse)
{
    Fixture f(EMPTY_OBJECT);
    EXPECT_DEATH(
        {
            f.AddLazyFrame(f.fastAllocation, f.merge);
            f.Verify();
        },
        "Allocation escapes into frame state");
    f.AddEagerFrame(f.slowAllocation);
    EXPECT_DEATH(f.Verify(), "Allocation escapes into frame state");
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

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsMismatchedGlueAndSize)
{
    for (auto kind: INITIALIZERS) {
        Fixture f;
        auto* init = f.Initialize(kind);
        constexpr uint32_t GLUE = InitializeEmptyObjectVertex::GLUE_INDEX;
        init->SetInput(GLUE, f.graph.GetIntPtrConstant(1));
        EXPECT_DEATH(f.Verify(), "");
        init->SetInput(GLUE, f.glue);
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
        // HClass, GlobalEnv and literal roots all occupy the third input.
        auto* unprotected = f.New<InitialValueVertex>({}, 0);
        init->SetInput(2, unprotected);
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
        init->SetInput(InitializeEmptyObjectVertex::GLUE_INDEX, f.hclass);
        EXPECT_DEATH(f.Verify(), "GetValueRepresentation\\(\\) == expectedRepr");
        init->SetInput(InitializeEmptyObjectVertex::GLUE_INDEX, f.glue);
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
    Fixture f;
    f.merge->AddVertex(f.Call());
    f.Initialize();
    f.ExpectRejectedInBothPhases("Illegal operation");
    f.merge->GetVertices().erase(f.merge->GetVertices().begin());
    f.fastBlock->AddVertex(f.New<AllocateInYoungVertex>({f.glue, f.size}));
    EXPECT_DEATH(f.Verify(), "Second allocation");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsOrdinaryLoadsEvenAfterRegalloc)
{
    Fixture f;
    f.merge->AddVertex(f.New<LoadTaggedFieldVertex>({f.hclass}, TaggedObject::HCLASS_OFFSET));
    f.Initialize();
    EXPECT_DEATH(f.Verify(true), "Illegal operation");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsDeoptAndBranchInsideInterval)
{
    Fixture f;
    f.AddEagerFrame(f.hclass);
    EXPECT_DEATH(f.Verify(), "Illegal operation");
    auto* target = BB::New(&f.chunk);
    f.graph.Add(target);
    f.merge->SetControlVertex(f.New<JumpVertex>({}, target));
    EXPECT_DEATH(f.Verify(), "Illegal operation");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsIncompleteAndCrossBlockIntervals)
{
    {
        Fixture f;
        EXPECT_DEATH(f.Verify(), "Incomplete initialization interval");
    }
    Fixture g;
    auto* init = g.Initialize();
    g.merge->GetVertices().pop_back();
    g.merge->GetPhis().clear();
    auto* later = g.AddSuccessor(g.merge);
    later->AddVertex(init);
    EXPECT_DEATH(g.Verify(), "Allocation Phi may only feed its matching initializer");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsAllocationBlockWithoutJumpToMerge)
{
    {
        Fixture f;
        f.fastBlock->SetControlVertex(nullptr);
        EXPECT_DEATH(f.Verify(), "Allocation block must end with a jump");
    }
    Fixture g;
    auto* stray = BB::New(&g.chunk);
    g.graph.Add(stray);
    g.slowBlock->SetControlVertex(g.New<JumpVertex>({}, stray));
    EXPECT_DEATH(g.Verify(), "Allocation must jump to its merge Phi block");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsInitializerBeforeAllocationAndDuplicateInitializer)
{
    Fixture f;
    auto* init = f.Initialize();
    f.fastBlock->AddVertex(init);
    EXPECT_DEATH(f.Verify(), "Allocation Phi may only feed its matching initializer");
    f.fastBlock->GetVertices().pop_back();
    f.Initialize();
    EXPECT_DEATH(f.Verify(), "Allocation Phi may only feed its matching initializer");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsDifferentAllocationAndNonAllocation)
{
    Fixture f;
    auto* init = f.Initialize();
    init->SetInput(InitializeEmptyObjectVertex::ALLOCATION_INDEX, f.New<AllocateInYoungVertex>({f.glue, f.size}));
    EXPECT_DEATH(f.Verify(), "Allocation escapes");
    init->SetInput(InitializeEmptyObjectVertex::ALLOCATION_INDEX, f.hclass);
    EXPECT_DEATH(f.Verify(), "");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsRepeatedScheduling)
{
    Fixture f(EMPTY_OBJECT);
    f.merge->AddVertex(f.fastAllocation);
    EXPECT_DEATH(f.Verify(), "Allocation scheduled more than once");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsAllocationAsConstantMoveSource)
{
    Fixture f;
    f.merge->AddVertex(f.New<ConstantGapMoveVertex>({}, f.fastAllocation, Fixture::Register()));
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
    EXPECT_DEATH(f.New<InitializeObjectWithBufferVertex>({f.phi, f.glue, f.hclass}, info), "fieldValues.size");
    info.fieldCount = 1;
    info.fieldValues = Span<const uint64_t>(nullptr, 1);
    EXPECT_DEATH(f.New<InitializeObjectWithBufferVertex>({f.phi, f.glue, f.hclass}, info), "fieldValues.data");
    info.fieldValues = Span<const uint64_t>(&f.field, 1);
    info.objectSize = JSObject::SIZE;
    EXPECT_DEATH(f.New<InitializeObjectWithBufferVertex>({f.phi, f.glue, f.hclass}, info), "fieldCount <=");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsWrongFixedInputCountAtConstruction)
{
    Fixture f;
    EXPECT_DEATH(f.New<AllocateInYoungVertex>({f.glue}), "GetInputCount");
    EXPECT_DEATH(f.New<InitializeEmptyObjectVertex>({f.phi, f.glue, f.hclass}, f.objectSize), "GetInputCount");
    auto info = f.Snapshot();
    EXPECT_DEATH(f.New<InitializeObjectWithBufferVertex>({f.phi, f.glue}, info), "GetInputCount");
    EXPECT_DEATH(f.New<InitializeEmptyArrayVertex>({f.phi, f.glue}, f.objectSize), "GetInputCount");
    EXPECT_DEATH(f.New<InitializeArrayWithBufferVertex>({f.phi, f.glue}, f.objectSize), "GetInputCount");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsDynamicAllocationSize)
{
    Fixture f;
    auto* size = f.New<LoadInt32FieldVertex>({f.hclass}, JSHClass::BIT_FIELD1_OFFSET);
    f.entry->GetVertices().insert(f.entry->GetVertices().begin(), size);
    f.canAllocate->SetInput(CanAllocateInYoungVertex::SIZE_INDEX, size);
    f.fastAllocation->SetInput(AllocateInYoungVertex::SIZE_INDEX, size);
    f.Initialize(EMPTY_ARRAY);
    EXPECT_DEATH(f.Verify(), "Is<T>");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsSecondMergePhiInInterval)
{
    Fixture f;
    auto* second = f.New<PhiVertex>({f.fastAllocation, f.slowAllocation}, VirtualRegister(1));
    f.merge->AddPhiVertex(second);
    f.Initialize();
    EXPECT_DEATH(f.Verify(), "Second allocation");
}
TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsMissingCapacityCheck)
{
    Fixture f(EMPTY_OBJECT);
    f.entry->SetControlVertex(f.New<JumpVertex>({}, f.fastBlock));
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
    EXPECT_DEATH(f.Verify(), "Capacity predicate must be scheduled in the checking block");
    auto* earlier = BB::New(&f.chunk);
    earlier->AddVertex(f.canAllocate);
    earlier->SetControlVertex(f.New<JumpVertex>({}, f.entry));
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
          static_cast<NonControlVertex*>(f.New<I32AddVertex>({f.size, f.size}))}) {
        f.entry->AddVertex(operation);
        f.ExpectRejectedInBothPhases("Only register moves may separate the capacity predicate and branch");
        f.entry->GetVertices().pop_back();
    }
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, AcceptsEffectsBeforePredicateAndMovesAfterRegalloc)
{
    Fixture f(EMPTY_OBJECT);
    f.entry->GetVertices().insert(f.entry->GetVertices().begin(), f.Call());
    f.Verify();
    auto slot = AllocatedState(LocationState::STACK_SLOT, MachineRepresentation::Tagged, 0);
    f.entry->AddVertex(f.New<GapMoveVertex>({}, Fixture::Register(), slot));
    f.entry->AddVertex(f.New<ConstantGapMoveVertex>({}, f.hclass, Fixture::Register()));
    f.fastBlock->GetVertices().insert(f.fastBlock->GetVertices().begin(),
                                      f.New<GapMoveVertex>({}, slot, Fixture::Register()));
    EXPECT_DEATH(f.Verify(), "Only register moves may separate the capacity predicate and branch");
    f.Verify(true);
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsRepeatedPredicateScheduling)
{
    Fixture f(EMPTY_OBJECT);
    f.entry->AddVertex(f.canAllocate);
    EXPECT_DEATH(f.Verify(), "Capacity predicate scheduled more than once");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsChangedGlueAndSizeAfterCheck)
{
    Fixture f(EMPTY_OBJECT);
    f.canAllocate->SetInput(CanAllocateInYoungVertex::GLUE_INDEX, f.graph.GetIntPtrConstant(1));
    EXPECT_DEATH(f.Verify(), "Allocation must match the checked glue and size");
    f.canAllocate->SetInput(CanAllocateInYoungVertex::GLUE_INDEX, f.glue);
    f.canAllocate->SetInput(CanAllocateInYoungVertex::SIZE_INDEX,
                            f.graph.GetInt32Constant(static_cast<int32_t>(f.objectSize + 8)));
    EXPECT_DEATH(f.Verify(), "Allocation must match the checked glue and size");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsReversedCapacityEdges)
{
    Fixture f(EMPTY_OBJECT);
    f.entry->SetControlVertex(f.New<BranchIfTrueVertex>({f.canAllocate}, f.slowBlock, f.fastBlock));
    EXPECT_DEATH(f.Verify(), "Fast allocation requires the successful check edge");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsCallsBetweenCheckAndAllocation)
{
    Fixture f(EMPTY_OBJECT);
    f.fastBlock->GetVertices().insert(f.fastBlock->GetVertices().begin(), f.Call());
    f.ExpectRejectedInBothPhases("Only register moves may separate the check and allocation");
}

TEST_F(ArkSteedInitializationIntervalVerifierTest, RejectsPhiPredecessorMismatch)
{
    Fixture f(EMPTY_OBJECT);
    f.phi->SetInput(0, f.slowAllocation);
    f.phi->SetInput(1, f.fastAllocation);
    EXPECT_DEATH(f.Verify(), "Allocation Phi input must match its predecessor");
    std::swap(f.merge->GetPredecessors()[0], f.merge->GetPredecessors()[1]);
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
