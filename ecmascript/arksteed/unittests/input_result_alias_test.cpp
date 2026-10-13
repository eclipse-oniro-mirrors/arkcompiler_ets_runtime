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

#include <array>

#include "ecmascript/arksteed/arksteed_assembler-inl.h"
#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_regalloc.h"
#include "ecmascript/arksteed/arksteed_regalloc_processors.h"
#include "ecmascript/arksteed/arksteed_side_effect_classifier.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_object.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/tests/test_helper.h"

namespace panda::ecmascript::arksteed {

// Connect each stand-alone test vertex to valid producers before applying its
// input constraints. Match the declared representation: INT_PTR constants use
// Int64ConstantVertex, while tagged inputs use TaggedConstantVertex.
static ValueVertex *NewConstantProducer(ValueRepresentation representation, Chunk &chunk)
{
    ValueVertex *producer;
    if (representation == ValueRepresentation::TAGGED) {
        producer = Vertex::New<TaggedConstantVertex>(
            &chunk, static_cast<size_t>(0), static_cast<uint64_t>(JSTaggedValue::VALUE_UNDEFINED));
    } else {
        producer = Vertex::New<Int64ConstantVertex>(&chunk, static_cast<size_t>(0), 0);
    }
    producer->SetRegallocInfo(chunk.New<RegallocValueVertexInfo>(
        &chunk, producer->GetInputCount(), producer->GetMachineRepresentation()));
    return producer;
}

// Exercise real input assignment and ordinary result selection under pressure.
class ArkSteedRegisterAllocatorTest : public testing::Test {
protected:
    static constexpr VertexId CURRENT_ID = 10;
    static constexpr VertexId LATER_ID = 20;
    static constexpr VertexId FURTHEST_ID = 30;
    using Policy = UnallocatedState::ExtendedPolicy;
    enum class Liveness { LAST_USE, DEOPT_USE, LIVE_AFTER };
    template<typename RegisterT>
    struct Fixture {
        NativeAreaAllocator native;
        Chunk chunk {&native};
        Graph graph {&chunk};
        ArkSteedRegisterAllocator allocator {&graph};
        ValueVertex* result;
        RegisterT first = GetAllocatableList<RegisterT>().First();
        RegisterT second = first;

        explicit Fixture(Liveness liveness = Liveness::LIVE_AFTER,
                         unsigned endCount = 2,
                         bool useAny = false,
                         bool duplicate = false,
                         bool assign = true)
        {
            auto available = GetAllocatableList<RegisterT>();
            available.Clear(first);
            second = available.First();
            if constexpr (std::is_same_v<RegisterT, ArkSteedRegister>) {
                result = Vertex::New<I32AddVertex>(&chunk, static_cast<size_t>(2));
            } else {
                result = Vertex::New<F64AddVertex>(&chunk, static_cast<size_t>(2));
            }
            InitInfo(result, CURRENT_ID);
            DefineAsRegister(result);
            allocator.currentVertex_ = result;

            for (RegisterT reg: GetAllocatableList<RegisterT>()) {
                ValueVertex* value = NewValue(reg.Code());
                InitInfo(value, 1);
                Registers().RemoveFromFree(reg);
                Registers().SetValue(reg, value);
                Registers().Unblock(reg);
            }
            result->SetInput(0, Registers().GetValue(first));
            result->SetInput(1, Registers().GetValue(duplicate ? first : second));
            for (unsigned i = 0; i < 2; ++i) {
                Input input = result->Arg(i);
                if (useAny) {
                    UseAny(input);
                } else if (i < endCount) {
                    UseRegister(input);
                } else {
                    UseAndClobberRegister(input);
                }
                input.vertex()->GetRegallocInfo()->RecordUse(CURRENT_ID, input.GetLocation());
            }
            // LAST_USE is freed by UpdateUse. DEOPT_USE stays mapped with one
            // additional use at this vertex; LIVE_AFTER needs the original value later.
            for (RegisterT reg: GetAllocatableList<RegisterT>()) {
                auto* value = Registers().GetValue(reg);
                bool isInput = value == result->GetInput(0) || value == result->GetInput(1);
                if (!isInput || liveness != Liveness::LAST_USE) {
                    VertexId next = isInput && liveness == Liveness::DEOPT_USE
                                        ? CURRENT_ID
                                        : (reg == first ? FURTHEST_ID : LATER_ID);
                    value->GetRegallocInfo()->RecordUse(next, chunk.New<InputLocation>());
                }
            }
            if (assign) {
                allocator.AssignInputs(result);
            }
        }

        ValueVertex* NewValue(int value)
        {
            if constexpr (std::is_same_v<RegisterT, ArkSteedRegister>) {
                return Vertex::New<Int32ConstantVertex>(&chunk, static_cast<size_t>(0), value);
            } else {
                return Vertex::New<Float64ConstantVertex>(
                    &chunk, static_cast<size_t>(0), static_cast<double>(value));
            }
        }

        void InitInfo(ValueVertex* vertex, VertexId id)
        {
            vertex->SetRegallocInfo(chunk.New<RegallocValueVertexInfo>(
                &chunk, vertex->GetInputCount(), vertex->GetMachineRepresentation()));
            vertex->SetId(id);
        }

        RegisterSnapshot<RegisterT>& Registers()
        {
            return allocator.GetRegisterSnapshot<RegisterT>();
        }

        void HintFirst()
        {
            constexpr Policy fixed =
                std::is_same_v<RegisterT, ArkSteedRegister> ? Policy::FIXED_REGISTER : Policy::FIXED_FP_REGISTER;
            result->SetHint(UnallocatedState(fixed, first.Code(), NO_VREG));
        }

        void DefineFixedResult()
        {
            constexpr Policy fixed =
                std::is_same_v<RegisterT, ArkSteedRegister> ? Policy::FIXED_REGISTER : Policy::FIXED_FP_REGISTER;
            result->Result().SetUnallocated(fixed, first.Code(), NO_VREG);
        }

        RegisterT Allocate()
        {
            allocator.AllocateVertexResult(result);
            if constexpr (std::is_same_v<RegisterT, ArkSteedRegister>) {
                return result->Result().GetAssignedGeneralRegister();
            } else {
                return result->Result().GetAssignedDoubleRegister();
            }
        }
    };

    template<typename RegisterT>
    static void RunResultSelectionTests()
    {
        for (Liveness liveness: {Liveness::LAST_USE, Liveness::DEOPT_USE, Liveness::LIVE_AFTER}) {
            for (bool hint: {false, true}) {
                Fixture<RegisterT> f(liveness);
                if (hint) {
                    f.HintFirst();
                }
                if (liveness == Liveness::LAST_USE) {
                    EXPECT_TRUE(f.Registers().Free().Has(f.first) && f.Registers().IsBlocked(f.first))
                        << "last use did not reach the free-and-blocked input path";
                }
                RegisterT reg = f.Allocate();
                EXPECT_TRUE(reg != f.first && reg != f.second)
                    << "result overwrote a USED_AT_END UseRegister input";
                if (liveness != Liveness::LAST_USE) {
                    EXPECT_TRUE(f.Registers().GetValue(f.first) == f.result->GetInput(0) &&
                                f.Registers().GetValue(f.second) == f.result->GetInput(1))
                        << "result spill selection moved a protected input";
                }
            }
        }
        for (unsigned endCount: {0U, 1U}) {
            Fixture<RegisterT> f(Liveness::LIVE_AFTER, endCount);
            f.HintFirst();
            ASSERT_TRUE(f.Registers().IsBlocked(f.first) && f.Registers().IsBlocked(f.second));
            RegisterT reg = f.Allocate();
            EXPECT_TRUE(reg != f.first && reg != f.second)
                << "ordinary result reused an allocated input despite conservative exclusion";
            EXPECT_TRUE(f.result->GetInput(1)->GetRegallocInfo()->IsSpilled())
                << "clobbering a USED_AT_START input lost its later live value";
            if (endCount == 0) {
                EXPECT_TRUE(f.result->GetInput(0)->GetRegallocInfo()->IsSpilled())
                    << "clobbering the first input lost its later live value";
            } else {
                EXPECT_TRUE(f.Registers().GetValue(f.first) == f.result->GetInput(0))
                    << "result allocation moved a USED_AT_END input";
            }
        }
        {
            Fixture<RegisterT> f(Liveness::LAST_USE, 2, false, true);
            EXPECT_TRUE(f.result->GetInputLocation(0)->GetOperand() == f.result->GetInputLocation(1)->GetOperand())
                << "duplicate inputs did not share a register";
            EXPECT_TRUE(f.Allocate() != f.first) << "duplicate last use bypassed result exclusion";
        }
        {
            Fixture<RegisterT> f(Liveness::LAST_USE);
            auto& temps = f.result->GetRegallocInfo()->template GetTemporaries<RegisterT>();
            for (unsigned i = 0; i < 3; ++i) {
                RegisterT reg = f.allocator.FreeUnblockedRegister(f.Registers(), RegListBase<RegisterT>());
                f.Registers().Block(reg);
                temps.Set(reg);
            }
            RegisterT reg = f.Allocate();
            EXPECT_TRUE(reg != f.first && reg != f.second && !temps.Has(reg))
                << "result reused an input or an allocation temporary";
        }
        {
            Fixture<RegisterT> f(Liveness::LAST_USE);
            // Free a non-input register through the allocator, retaining the
            // blocked last-use inputs as ordinary input assignment leaves them.
            RegisterT freeReg = f.allocator.FreeUnblockedRegister(f.Registers(), RegListBase<RegisterT>());
            ASSERT_TRUE(freeReg != f.first && freeReg != f.second);
            ASSERT_TRUE(f.Registers().UnblockedFree().Has(freeReg));
            ASSERT_TRUE(f.Registers().IsBlocked(f.first) && f.Registers().IsBlocked(f.second));
            f.HintFirst();
            EXPECT_TRUE(f.Allocate() == freeReg)
                << "free-list allocation did not use the available non-input register";
        }
        {
            Fixture<RegisterT> f;
            f.Allocate();
            f.result = f.NewValue(7);
            f.InitInfo(f.result, CURRENT_ID + 1);
            DefineAsRegister(f.result);
            f.HintFirst();
            f.allocator.currentVertex_ = f.result;
            f.allocator.AssignInputs(f.result);
            EXPECT_TRUE(f.Allocate() == f.first) << "result exclusion leaked to the next vertex";
        }
    }

    template<typename RegisterT>
    static void CheckExistingPolicies()
    {
        // This fix must not reinterpret explicit fixed or tied result policies.
        for (bool tied: {false, true}) {
            Fixture<RegisterT> f;
            if (tied) {
                DefineSameAsFirst(f.result);
            } else {
                f.DefineFixedResult();
            }
            EXPECT_TRUE(f.Allocate() == f.first) << "explicit fixed/tied result policy changed";
        }
        {
            Fixture<RegisterT> f(Liveness::LAST_USE, 2, true);
            f.HintFirst();
            ASSERT_TRUE(f.result->GetInputLocation(0)->IsAnyRegister()
                        && f.result->GetInputLocation(1)->IsAnyRegister());
            ASSERT_TRUE(f.Registers().Free().Has(f.first) && f.Registers().Free().Has(f.second));
            ASSERT_TRUE(f.Registers().IsBlocked(f.first) && f.Registers().IsBlocked(f.second));
            RegisterT reg = f.Allocate();
            EXPECT_TRUE(reg != f.first && reg != f.second)
                << "ordinary result reused an allocated UseAny input on its last use";
        }
        {
            Fixture<RegisterT> f;
            // Match the call allocator sequence without changing input declarations.
            f.allocator.SpillAndClearRegisters();
            f.DefineFixedResult();
            EXPECT_TRUE(f.Allocate() == f.first) << "call register clearing acquired a lifetime check";
        }
        {
            Fixture<RegisterT> f(Liveness::LIVE_AFTER, 2, false, false, false);
            if constexpr (std::is_same_v<RegisterT, ArkSteedRegister>) {
                f.result->template Cast<I32AddVertex>()->SetValueLocationConstraints();
            } else {
                f.result->template Cast<F64AddVertex>()->SetValueLocationConstraints();
            }
            EXPECT_TRUE(!f.result->GetInputLocation(0)->IsClobbered() &&
                        !f.result->GetInputLocation(1)->IsClobbered())
                << "existing in-place arithmetic input declarations changed";
            f.allocator.AssignInputs(f.result);
            EXPECT_TRUE(f.Allocate() == f.first) << "existing in-place arithmetic could not reuse its input";
        }
    }

    template<typename Allocation>
    static void CheckAllocationRegisters()
    {
        NativeAreaAllocator native;
        Chunk chunk {&native};
        Graph graph {&chunk};
        ArkSteedRegisterAllocator allocator {&graph};
        auto* allocation = [&]() {
            if constexpr (std::is_same_v<Allocation, CallRuntimeVertex>) {
                return Vertex::New<Allocation>(&chunk, static_cast<size_t>(1), RTSTUB_ID(AllocateInYoung));
            } else {
                return Vertex::New<Allocation>(&chunk, static_cast<size_t>(Allocation::NUM_INPUTS));
            }
        }();
        auto initInfo = [&](ValueVertex* value) {
            value->SetRegallocInfo(chunk.New<RegallocValueVertexInfo>(
                &chunk, value->GetInputCount(), value->GetMachineRepresentation()));
            value->SetId(1);
        };
        initInfo(allocation);
        allocation->SetId(CURRENT_ID);
        auto* glue = graph.GetIntPtrConstant(0);
        auto* size = graph.GetInt32Constant(JSObject::SIZE);
        initInfo(glue);
        initInfo(size);
        if constexpr (std::is_same_v<Allocation, CallRuntimeVertex>) {
            auto* taggedSize
                = graph.GetTaggedConstant(JSTaggedValue(static_cast<int32_t>(JSObject::SIZE)).GetRawData());
            initInfo(taggedSize);
            allocation->SetInput(0, taggedSize);
            allocation->VerifyInputs();
        } else {
            allocation->SetInput(Allocation::GLUE_INDEX, glue);
            allocation->SetInput(Allocation::SIZE_INDEX, size);
        }
        allocator.SetupConstantLocations();
        for (uint32_t i = 0; i < allocation->GetInputCount(); ++i) {
            allocation->GetInput(i)->GetRegallocInfo()->RecordUse(CURRENT_ID, allocation->GetInputLocation(i));
        }
        ChunkVector<ValueVertex*> roots(&chunk);
        auto& registers = allocator.GetRegisterSnapshot<ArkSteedRegister>();
        for (auto reg: GetAllocatableList<ArkSteedRegister>()) {
            auto* value = Vertex::New<InitialValueVertex>(&chunk, static_cast<size_t>(0), 0);
            initInfo(value);
            value->GetRegallocInfo()->RecordUse(LATER_ID, chunk.New<InputLocation>());
            registers.RemoveFromFree(reg);
            registers.SetValue(reg, value);
            registers.Unblock(reg);
            roots.push_back(value);
        }
        auto* number = Vertex::New<Float64ConstantVertex>(&chunk, static_cast<size_t>(0), 1.25);
        initInfo(number);
        number->GetRegallocInfo()->RecordUse(LATER_ID, chunk.New<InputLocation>());
        auto& doubles = allocator.GetRegisterSnapshot<ArkSteedDoubleRegister>();
        auto fpReg = GetAllocatableList<ArkSteedDoubleRegister>().First();
        doubles.RemoveFromFree(fpReg);
        doubles.SetValue(fpReg, number);
        doubles.Unblock(fpReg);
        allocation->GetRegallocInfo()->RecordUse(LATER_ID, chunk.New<InputLocation>());
        allocation->SetValueLocationConstraints();
        allocator.AllocateVertex(allocation);
        auto result = allocation->Result().GetAssignedGeneralRegister();
        if constexpr (std::is_same_v<Allocation, CallRuntimeVertex>) {
            EXPECT_EQ(result, kReturnRegister);
        }
        for (uint32_t i = 0; i < allocation->GetInputCount(); ++i) {
            auto input = allocation->GetInputLocation(i)->GetAssignedGeneralRegister();
            if constexpr (std::is_same_v<Allocation, CallRuntimeVertex>) {
                EXPECT_EQ(input, ArkSteedAssembler::GetParameterRegister(static_cast<int>(i)));
            } else {
                EXPECT_NE(input, result);
            }
            EXPECT_FALSE(allocation->GetRegallocInfo()->GetGeneralTemporaries().Has(input));
        }
        EXPECT_FALSE(allocation->GetRegallocInfo()->GetGeneralTemporaries().Has(result));
        if constexpr (std::is_same_v<Allocation, CallRuntimeVertex>) {
            for (auto* root: roots) {
                ASSERT_TRUE(root->GetRegallocInfo()->IsSpilled());
                EXPECT_EQ(AllocatedState::Cast(root->GetRegallocInfo()->GetSpillSlot()).GetRepresentation(),
                          MachineRepresentation::Tagged);
                EXPECT_FALSE(root->GetRegallocInfo()->HasRegisterResult());
            }
            EXPECT_FALSE(number->GetRegallocInfo()->HasRegisterResult());
            ASSERT_TRUE(number->GetRegallocInfo()->IsSpilled());
            EXPECT_EQ(AllocatedState::Cast(number->GetRegallocInfo()->GetSpillSlot()).GetRepresentation(),
                      MachineRepresentation::Float64);
        } else {
            EXPECT_TRUE(number->GetRegallocInfo()->HasRegisterResult());
            EXPECT_FALSE(number->GetRegallocInfo()->IsSpilled());
            bool hasLiveRegister = false;
            for (auto* root: roots) {
                hasLiveRegister |= root->GetRegallocInfo()->HasRegisterResult();
            }
            EXPECT_TRUE(hasLiveRegister);
        }
    }

    template<class VertexT>
    static void CheckInitializationConstraints(VertexT* vertex, Chunk& chunk, unsigned temporaries)
    {
        vertex->SetRegallocInfo(chunk.New<RegallocValueVertexInfo>(
            &chunk, vertex->GetInputCount(), vertex->GetMachineRepresentation()));
        vertex->SetValueLocationConstraints();
        auto result = UnallocatedState::Cast(vertex->Result().GetOperand());
        ASSERT_EQ(result.GetExtendedPolicy(), Policy::SAME_AS_INPUT);
        EXPECT_EQ(result.GetInputIndex(), VertexT::ALLOCATION_INDEX);
        EXPECT_EQ(VertexT::ALLOCATION_INDEX, 0U);
        EXPECT_EQ(vertex->GetValueRepresentation(), ValueRepresentation::TAGGED);
        EXPECT_FALSE(vertex->IsCall());
        EXPECT_FALSE(vertex->IsDeferredCall());
        EXPECT_FALSE(vertex->CanAllocate());
        EXPECT_FALSE(vertex->CanThrow());
        EXPECT_FALSE(vertex->CanEagerDeopt());
        EXPECT_FALSE(vertex->CanLazyDeopt());
        EXPECT_EQ(VertexT::INPUT_TYPES[VertexT::ALLOCATION_INDEX], ValueRepresentation::TAGGED);
        EXPECT_EQ(VertexT::INPUT_TYPES[VertexT::GLUE_INDEX], ValueRepresentation::INT_PTR);
        EXPECT_EQ(VertexT::INPUT_TYPES[2], ValueRepresentation::TAGGED);
        if constexpr (std::is_same_v<VertexT, InitializeEmptyObjectVertex>) {
            EXPECT_EQ(VertexT::INPUT_TYPES[VertexT::SIZE_INDEX], ValueRepresentation::INT32);
        }
        for (unsigned i = 0; i < vertex->GetInputCount(); ++i) {
            auto operand = UnallocatedState::Cast(vertex->GetInputLocation(i)->GetOperand());
            EXPECT_TRUE(operand.GetExtendedPolicy() == Policy::MUST_HAVE_REGISTER && !operand.IsUsedAtStart())
                << "initializer input does not use ordinary UseRegister";
        }
        EXPECT_TRUE(
            vertex->template GetNumTemporariesNeeded<ArkSteedRegister>() == temporaries
            && vertex->GetRegallocInfo()->template GetRequiredSpecificTemporaries<ArkSteedRegister>().IsEmpty())
            << "initializer has unexpected temporaries";

        Graph graph {&chunk};
        ArkSteedRegisterAllocator allocator {&graph};
        auto& registers = allocator.GetRegisterSnapshot<ArkSteedRegister>();
        unsigned index = 0;
        for (auto reg: GetAllocatableList<ArkSteedRegister>()) {
            ValueVertex* value;
            if (index < VertexT::NUM_INPUTS && VertexT::INPUT_TYPES[index] == ValueRepresentation::INT32) {
                value = Vertex::New<Int32ConstantVertex>(&chunk, static_cast<size_t>(0), 64);
            } else if (index == VertexT::GLUE_INDEX) {
                value = Vertex::New<IntPtrConstantVertex>(&chunk, static_cast<size_t>(0), 0);
            } else {
                value = Vertex::New<InitialValueVertex>(&chunk, static_cast<size_t>(0), 0);
            }
            value->SetRegallocInfo(chunk.New<RegallocValueVertexInfo>(
                &chunk, value->GetInputCount(), value->GetMachineRepresentation()));
            value->SetId(1);
            registers.RemoveFromFree(reg);
            registers.SetValue(reg, value);
            registers.Unblock(reg);
            if (index < VertexT::NUM_INPUTS) {
                vertex->SetInput(index, value);
                value->GetRegallocInfo()->RecordUse(CURRENT_ID, vertex->GetInputLocation(index));
            } else {
                value->GetRegallocInfo()->RecordUse(LATER_ID, chunk.New<InputLocation>());
            }
            ++index;
        }
        ASSERT_GE(index, VertexT::NUM_INPUTS);
        vertex->SetId(CURRENT_ID);
        allocator.currentVertex_ = vertex;
        allocator.AssignInputs(vertex);
        auto allocationRegister
            = vertex->Arg(VertexT::ALLOCATION_INDEX).GetLocation()->GetAssignedGeneralRegister();
        allocator.AllocateVertexResult(vertex);
        EXPECT_EQ(vertex->Result().GetAssignedGeneralRegister(), allocationRegister);
        for (uint32_t i = 1; i < VertexT::NUM_INPUTS; ++i) {
            EXPECT_NE(vertex->Arg(i).GetLocation()->GetAssignedGeneralRegister(), allocationRegister)
                << "initializer result overwrote a layout input under register pressure";
        }
    }

    // =========================================================================
    // G6.2u: stack-slot pool (SpillLocations / AllocateSpillSlot)
    // =========================================================================

    // A spillable value vertex with an explicitly chosen live range [start, end]
    // plus the real allocator on a Chunk-backed graph. The policy under test
    // (arksteed_regalloc.cpp, AllocateSpillSlot) reuses the newest freed slot
    // whose freedAtPosition is before the new live-range start; a dead value is
    // returned to the per-representation pool by UpdateUse.
    struct SpillFixture {
        NativeAreaAllocator native;
        Chunk chunk {&native};
        Graph graph {&chunk};
        ArkSteedRegisterAllocator allocator {&graph};
        ValueVertex* NewValue(MachineRepresentation rep, VertexId start, VertexId end)
        {
            ValueVertex* vertex;
            if (rep == MachineRepresentation::Tagged) {
                vertex = Vertex::New<TaggedConstantVertex>(&chunk, static_cast<size_t>(0),
                                                           static_cast<uint64_t>(0));
            } else if (rep == MachineRepresentation::Float64) {
                vertex = Vertex::New<Float64ConstantVertex>(&chunk, static_cast<size_t>(0), 0.0);
            } else {
                vertex = Vertex::New<Int32ConstantVertex>(&chunk, static_cast<size_t>(0), 0);
            }
            // The info owns one input location: RecordUse needs it to install the
            // next-use chain that UpdateUse observes at the end of the range.
            auto* info = chunk.New<RegallocValueVertexInfo>(&chunk, 1, rep);
            vertex->SetRegallocInfo(info);
            vertex->SetId(start);
            info->RecordUse(end, info->GetInputLocation(0));
            return vertex;
        }

        void Allocate(ValueVertex* vertex)
        {
            allocator.AllocateSpillSlot(vertex);
        }

        void Free(ValueVertex* vertex)
        {
            allocator.UpdateUse(vertex, vertex->GetRegallocInfo()->GetInputLocation(0));
        }

        static int32_t SlotOf(ValueVertex* vertex)
        {
            return AllocatedState::Cast(vertex->GetRegallocInfo()->GetSpillSlot()).GetIndex();
        }

        // Slots currently occupied = every slot ever handed out (top) minus the
        // slots sitting in the reuse pool.
        static int Occupied(const ArkSteedRegisterAllocator& allocator, bool tagged)
        {
            const auto& slots = tagged ? allocator.tagged_ : allocator.untagged_;
            return slots.top - static_cast<int>(slots.freeSlots.size());
        }
    };

    static void RunSpillPressureAndPoolAccounting()
    {
        SpillFixture f;
        // Four simultaneously live values: no slot has been freed yet, so the
        // pool has to grow to the maximum pressure of the fixture.
        ValueVertex* values[4];
        for (unsigned i = 0; i < 4; ++i) {
            values[i] = f.NewValue(MachineRepresentation::Word32, 10, 20 + i);
            f.Allocate(values[i]);
            EXPECT_EQ(SpillFixture::SlotOf(values[i]), static_cast<int32_t>(i))
                << "concurrently live values did not receive distinct slots";
            EXPECT_EQ(SpillFixture::Occupied(f.allocator, false), static_cast<int>(i) + 1)
                << "occupied slot count diverged from live pressure";
        }
        EXPECT_EQ(f.allocator.untagged_.top, 4);
        EXPECT_TRUE(f.allocator.untagged_.freeSlots.empty());

        // Freeing returns every slot with its exact freedAtPosition.
        for (ValueVertex* value : values) {
            f.Free(value);
        }
        EXPECT_EQ(SpillFixture::Occupied(f.allocator, false), 0);
        ASSERT_EQ(f.allocator.untagged_.freeSlots.size(), 4u);
        for (unsigned i = 0; i < 4; ++i) {
            EXPECT_EQ(f.allocator.untagged_.freeSlots[i].slotIndex, i);
            EXPECT_EQ(f.allocator.untagged_.freeSlots[i].freedAtPosition, 20u + i);
            EXPECT_FALSE(f.allocator.untagged_.freeSlots[i].doubleSlot);
        }

        // A later value reuses exactly one pooled slot; the pool size stays at
        // the historical maximum pressure instead of the current occupancy.
        ValueVertex* reused = f.NewValue(MachineRepresentation::Word32, 30, 40);
        f.Allocate(reused);
        EXPECT_EQ(SpillFixture::Occupied(f.allocator, false), 1);
        EXPECT_EQ(f.allocator.untagged_.top, 4);
    }

    static void RunSpillSlotReusePolicy()
    {
        SpillFixture f;
        // Slot 0 is freed at 20; a value whose range starts at 10 cannot use it.
        ValueVertex* first = f.NewValue(MachineRepresentation::Word32, 10, 20);
        f.Allocate(first);
        EXPECT_EQ(SpillFixture::SlotOf(first), 0);
        f.Free(first);

        ValueVertex* second = f.NewValue(MachineRepresentation::Word32, 10, 25);
        f.Allocate(second);
        EXPECT_EQ(SpillFixture::SlotOf(second), 1)
            << "a slot freed at 20 was reused by a range starting at 10";
        f.Free(second);

        ASSERT_EQ(f.allocator.untagged_.freeSlots.size(), 2u);
        EXPECT_EQ(f.allocator.untagged_.freeSlots[0].freedAtPosition, 20u);
        EXPECT_EQ(f.allocator.untagged_.freeSlots[1].freedAtPosition, 25u);

        // Both pooled slots are reusable from position 30; the newest one wins.
        ValueVertex* newest = f.NewValue(MachineRepresentation::Word32, 30, 40);
        f.Allocate(newest);
        EXPECT_EQ(SpillFixture::SlotOf(newest), 1) << "the newest freed slot was not reused first";
        EXPECT_EQ(f.allocator.untagged_.top, 2);

        ValueVertex* older = f.NewValue(MachineRepresentation::Word32, 30, 40);
        f.Allocate(older);
        EXPECT_EQ(SpillFixture::SlotOf(older), 0) << "the older reusable slot was not reused next";
        EXPECT_EQ(f.allocator.untagged_.top, 2);
        EXPECT_TRUE(f.allocator.untagged_.freeSlots.empty());
    }

    static void RunSpillSlotOverlapRejection()
    {
        SpillFixture f;
        ValueVertex* overlapping = f.NewValue(MachineRepresentation::Word32, 10, 40);
        f.Allocate(overlapping);
        EXPECT_EQ(SpillFixture::SlotOf(overlapping), 0);
        f.Free(overlapping);
        ASSERT_EQ(f.allocator.untagged_.freeSlots.size(), 1u);
        EXPECT_EQ(f.allocator.untagged_.freeSlots[0].freedAtPosition, 40u);

        // The new range starts at 30, i.e. before the pooled slot is freed at
        // 40: reuse must be refused even though the slot exists.
        ValueVertex* live = f.NewValue(MachineRepresentation::Word32, 30, 50);
        f.Allocate(live);
        EXPECT_EQ(SpillFixture::SlotOf(live), 1) << "an overlapping live range reused a pooled slot";
        EXPECT_EQ(f.allocator.untagged_.top, 2);
        EXPECT_EQ(f.allocator.untagged_.freeSlots.size(), 1u);
        EXPECT_EQ(f.allocator.untagged_.freeSlots[0].slotIndex, 0u);
    }

    static void RunTaggedAndUntaggedPoolsAreIndependent()
    {
        SpillFixture f;
        ValueVertex* tagged = f.NewValue(MachineRepresentation::Tagged, 10, 20);
        f.Allocate(tagged);
        ValueVertex* untagged = f.NewValue(MachineRepresentation::Word32, 10, 20);
        f.Allocate(untagged);
        // Both are index 0 because they live in separate pools.
        EXPECT_EQ(SpillFixture::SlotOf(tagged), 0);
        EXPECT_EQ(SpillFixture::SlotOf(untagged), 0);
        EXPECT_EQ(f.allocator.tagged_.top, 1);
        EXPECT_EQ(f.allocator.untagged_.top, 1);

        f.Free(tagged);
        f.Free(untagged);
        EXPECT_EQ(f.allocator.tagged_.freeSlots.size(), 1u);
        EXPECT_EQ(f.allocator.untagged_.freeSlots.size(), 1u)
            << "freeing a tagged value polluted the untagged pool";
        EXPECT_EQ(f.allocator.tagged_.freeSlots[0].freedAtPosition, 20u);
        EXPECT_EQ(f.allocator.untagged_.freeSlots[0].freedAtPosition, 20u);

        ValueVertex* reusedTagged = f.NewValue(MachineRepresentation::Tagged, 30, 40);
        f.Allocate(reusedTagged);
        EXPECT_EQ(SpillFixture::SlotOf(reusedTagged), 0);
        EXPECT_EQ(f.allocator.tagged_.top, 1);
        EXPECT_TRUE(f.allocator.tagged_.freeSlots.empty());
        EXPECT_EQ(f.allocator.untagged_.freeSlots.size(), 1u)
            << "tagged reuse consumed a slot from the untagged pool";

        ValueVertex* reusedUntagged = f.NewValue(MachineRepresentation::Word32, 30, 40);
        f.Allocate(reusedUntagged);
        EXPECT_EQ(SpillFixture::SlotOf(reusedUntagged), 0);
        EXPECT_EQ(f.allocator.untagged_.top, 1);
        EXPECT_TRUE(f.allocator.untagged_.freeSlots.empty());
    }

    static void RunDoubleAndPointerSlotsDoNotMix()
    {
        SpillFixture f;
        ValueVertex* pointer = f.NewValue(MachineRepresentation::Word32, 10, 20);
        f.Allocate(pointer);
        EXPECT_EQ(SpillFixture::SlotOf(pointer), 0);
        f.Free(pointer);

        // Untagged slot 0 is pointer-width and freed at 20; a double value whose
        // range starts at 30 must not take it.
        ValueVertex* firstDouble = f.NewValue(MachineRepresentation::Float64, 30, 40);
        f.Allocate(firstDouble);
        EXPECT_EQ(SpillFixture::SlotOf(firstDouble), 1) << "a double value reused a pointer-width slot";
        f.Free(firstDouble);

        ASSERT_EQ(f.allocator.untagged_.freeSlots.size(), 2u);
        EXPECT_FALSE(f.allocator.untagged_.freeSlots[0].doubleSlot);
        EXPECT_TRUE(f.allocator.untagged_.freeSlots[1].doubleSlot);

        ValueVertex* secondDouble = f.NewValue(MachineRepresentation::Float64, 50, 60);
        f.Allocate(secondDouble);
        EXPECT_EQ(SpillFixture::SlotOf(secondDouble), 1) << "the double value missed the pooled double slot";
        EXPECT_EQ(f.allocator.untagged_.top, 2);

        ValueVertex* secondPointer = f.NewValue(MachineRepresentation::Word32, 50, 60);
        f.Allocate(secondPointer);
        EXPECT_EQ(SpillFixture::SlotOf(secondPointer), 0) << "a pointer value reused a double slot";
        EXPECT_EQ(f.allocator.untagged_.top, 2);
        EXPECT_TRUE(f.allocator.untagged_.freeSlots.empty());
    }

    static void RunReuseStackSlotsOption()
    {
        {
            // Allocation gate: a pooled slot is not consumed while reuse is off.
            SpillFixture f;
            ValueVertex* freed = f.NewValue(MachineRepresentation::Word32, 10, 20);
            f.Allocate(freed);
            f.Free(freed);
            ASSERT_EQ(f.allocator.untagged_.freeSlots.size(), 1u);

            f.graph.SetReuseStackSlots(false);
            ValueVertex* next = f.NewValue(MachineRepresentation::Word32, 30, 40);
            f.Allocate(next);
            EXPECT_EQ(SpillFixture::SlotOf(next), 1)
                << "reuse of a pooled slot survived SetReuseStackSlots(false)";
            EXPECT_EQ(f.allocator.untagged_.top, 2);
            EXPECT_EQ(f.allocator.untagged_.freeSlots.size(), 1u);
        }
        {
            // Free gate: with reuse off, a dead value is not returned to the pool.
            SpillFixture f;
            f.graph.SetReuseStackSlots(false);
            ValueVertex* dead = f.NewValue(MachineRepresentation::Word32, 10, 20);
            f.Allocate(dead);
            EXPECT_EQ(SpillFixture::SlotOf(dead), 0);
            f.Free(dead);
            EXPECT_TRUE(f.allocator.untagged_.freeSlots.empty())
                << "a dead value entered the pool while reuse was off";

            ValueVertex* next = f.NewValue(MachineRepresentation::Word32, 30, 40);
            f.Allocate(next);
            EXPECT_EQ(SpillFixture::SlotOf(next), 1);
            EXPECT_EQ(f.allocator.untagged_.top, 2);
        }
    }

    static void RunSpillSlotNegativeControl()
    {
        // Control: a slot freed at 20 is reusable by a range starting at 30.
        SpillFixture control;
        ValueVertex* released = control.NewValue(MachineRepresentation::Word32, 10, 20);
        control.Allocate(released);
        control.Free(released);
        ValueVertex* controlNext = control.NewValue(MachineRepresentation::Word32, 30, 40);
        control.Allocate(controlNext);

        // Mutation: shift the recorded freedAtPosition past the new range start.
        // The reuse decision has to flip, so a test that expected the control
        // slot number without observing freedAtPosition would stay green here
        // and could not detect the change.
        SpillFixture mutated;
        ValueVertex* shifted = mutated.NewValue(MachineRepresentation::Word32, 10, 20);
        mutated.Allocate(shifted);
        mutated.Free(shifted);
        ASSERT_EQ(mutated.allocator.untagged_.freeSlots.size(), 1u);
        mutated.allocator.untagged_.freeSlots[0].freedAtPosition = 40;
        ValueVertex* mutatedNext = mutated.NewValue(MachineRepresentation::Word32, 30, 40);
        mutated.Allocate(mutatedNext);

        EXPECT_EQ(SpillFixture::SlotOf(controlNext), 0);
        EXPECT_EQ(control.allocator.untagged_.top, 1);
        EXPECT_TRUE(control.allocator.untagged_.freeSlots.empty());
        EXPECT_EQ(mutated.allocator.untagged_.top, 2);
        EXPECT_EQ(SpillFixture::SlotOf(mutatedNext), 1)
            << "reuse ignored freedAtPosition, so the mutation was not detected";
        EXPECT_NE(SpillFixture::SlotOf(controlNext), SpillFixture::SlotOf(mutatedNext));
    }

    // Runs the real input and temporary assignment for the unmapped-arguments
    // initializer under full register pressure and verifies the core contract:
    // a temporary must never land on a register that holds one of the node's
    // ordinary UseRegister inputs.
    static void RunUnmappedArgumentsTemporariesNeverAliasInputs()
    {
        NativeAreaAllocator native;
        Chunk chunk {&native};
        Graph graph {&chunk};
        ArkSteedRegisterAllocator allocator {&graph};
        // Constants created through the graph are registered in its constant
        // tables, so SetupConstantLocations can mark them loadable when the
        // assignment below has to evict them.
        auto newGraphConstant = [&graph, &chunk](bool tagged, int64_t value) {
            ValueVertex *constant = tagged
                ? graph.GetTaggedConstant(static_cast<uint64_t>(value))
                : graph.GetInt64Constant(value);
            if (constant->GetRegallocInfo() == nullptr) {
                constant->SetRegallocInfo(chunk.New<RegallocValueVertexInfo>(
                    &chunk, constant->GetInputCount(), constant->GetMachineRepresentation()));
                constant->SetId(1);
            }
            return constant;
        };

        auto *initialize = Vertex::New<InitializeUnmappedArgumentsVertex>(
            &chunk, static_cast<size_t>(InitializeUnmappedArgumentsVertex::NUM_INPUTS));
        initialize->SetRegallocInfo(chunk.New<RegallocValueVertexInfo>(
            &chunk, initialize->GetInputCount(), initialize->GetMachineRepresentation()));
        initialize->SetId(CURRENT_ID);
        for (uint32_t i = 0; i < initialize->GetInputCount(); ++i) {
            bool tagged = InitializeUnmappedArgumentsVertex::INPUT_TYPES[i] ==
                          ValueRepresentation::TAGGED;
            ValueVertex *producer = newGraphConstant(
                tagged, tagged ? static_cast<int64_t>(JSTaggedValue::VALUE_UNDEFINED)
                               : static_cast<int64_t>(i));
            initialize->SetInput(i, producer);
        }
        initialize->SetValueLocationConstraints();
        for (uint32_t i = 0; i < initialize->GetInputCount(); ++i) {
            Input input = initialize->Arg(i);
            input.vertex()->GetRegallocInfo()->RecordUse(CURRENT_ID, input.GetLocation());
        }

        // Occupy every allocatable register with a live, spillable constant so
        // the temporary assignment must go through eviction. Trivially free
        // registers would weaken the aliasing check.
        for (ArkSteedRegister reg : GetAllocatableList<ArkSteedRegister>()) {
            ValueVertex *value = newGraphConstant(false, 1000 + reg.Code());
            value->GetRegallocInfo()->RecordUse(FURTHEST_ID, chunk.New<InputLocation>());
            auto &registers = allocator.GetRegisterSnapshot<ArkSteedRegister>();
            registers.RemoveFromFree(reg);
            registers.SetValue(reg, value);
            registers.Unblock(reg);
        }
        allocator.SetupConstantLocations();

        allocator.currentVertex_ = initialize;
        allocator.AssignInputs(initialize);

        ArkSteedRegList inputRegisters;
        for (uint32_t i = 0; i < initialize->GetInputCount(); ++i) {
            auto allocated = AllocatedState::Cast(initialize->GetInputLocation(i)->GetOperand());
            EXPECT_TRUE(allocated.IsRegister()) << "initializer input did not receive a register";
            inputRegisters.Set(allocated.GetRegister());
        }
        const ArkSteedRegList &temporaries = initialize->GetRegallocInfo()->GetGeneralTemporaries();
        EXPECT_EQ(temporaries.Count(), 3U);
        EXPECT_TRUE((inputRegisters & temporaries).IsEmpty())
            << "initializer temporary was assigned over an input register";
    }
};

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, GeneralResultSelection)
{
    RunResultSelectionTests<ArkSteedRegister>();
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, DoubleResultSelection)
{
    RunResultSelectionTests<ArkSteedDoubleRegister>();
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, GeneralExistingPolicies)
{
    CheckExistingPolicies<ArkSteedRegister>();
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, DoubleExistingPolicies)
{
    CheckExistingPolicies<ArkSteedDoubleRegister>();
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, YoungAllocationConstraints)
{
    NativeAreaAllocator native;
    Chunk chunk {&native};
    Graph graph {&chunk};
    auto* allocation
        = Vertex::New<AllocateInYoungVertex>(&chunk, static_cast<size_t>(AllocateInYoungVertex::NUM_INPUTS));
    allocation->SetInput(AllocateInYoungVertex::GLUE_INDEX, graph.GetIntPtrConstant(0));
    allocation->SetInput(AllocateInYoungVertex::SIZE_INDEX, graph.GetInt32Constant(JSObject::SIZE));
    allocation->SetRegallocInfo(chunk.New<RegallocValueVertexInfo>(
        &chunk, allocation->GetInputCount(), allocation->GetMachineRepresentation()));
    for (uint32_t i = 0; i < allocation->GetInputCount(); ++i) {
        auto* input = allocation->GetInput(i);
        input->SetRegallocInfo(
            chunk.New<RegallocValueVertexInfo>(&chunk, input->GetInputCount(), input->GetMachineRepresentation()));
    }
    allocation->SetValueLocationConstraints();
    auto result = UnallocatedState::Cast(allocation->Result().GetOperand());
    ASSERT_EQ(result.GetExtendedPolicy(), Policy::MUST_HAVE_REGISTER);
    for (uint32_t i = 0; i < allocation->GetInputCount(); ++i) {
        auto input = UnallocatedState::Cast(allocation->GetInputLocation(i)->GetOperand());
        EXPECT_EQ(input.GetExtendedPolicy(), Policy::MUST_HAVE_REGISTER);
        EXPECT_FALSE(input.IsUsedAtStart());
        EXPECT_EQ(allocation->GetInput(i)->GetValueRepresentation(), AllocateInYoungVertex::INPUT_TYPES[i]);
    }
    EXPECT_EQ(AllocateInYoungVertex::INPUT_TYPES[AllocateInYoungVertex::GLUE_INDEX], ValueRepresentation::INT_PTR);
    EXPECT_EQ(AllocateInYoungVertex::INPUT_TYPES[AllocateInYoungVertex::SIZE_INDEX], ValueRepresentation::INT32);
    EXPECT_EQ(allocation->GetValueRepresentation(), ValueRepresentation::TAGGED);
    EXPECT_EQ(allocation->GetNumTemporariesNeeded<ArkSteedRegister>(), 1U);
    EXPECT_TRUE(allocation->GetRegallocInfo()->GetRequiredSpecificTemporaries<ArkSteedRegister>().IsEmpty());
    EXPECT_FALSE(allocation->IsCall());
    EXPECT_TRUE(allocation->CanAllocate());
    EXPECT_FALSE(allocation->CanRead());
    EXPECT_FALSE(allocation->CanWrite());
    EXPECT_TRUE(allocation->IsNotIdempotent());
    EXPECT_FALSE(allocation->IsDeferredCall());
    EXPECT_FALSE(allocation->CanThrow());
    EXPECT_FALSE(allocation->CanEagerDeopt());
    EXPECT_FALSE(allocation->CanLazyDeopt());
    EXPECT_EQ(ArkSteedSideEffectClassifier::Classify(allocation).kind, SideEffectKind::NO_SIDE_EFFECT);
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, AllocationCallPreservesLiveValues)
{
    CheckAllocationRegisters<CallRuntimeVertex>();
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, FastAllocationPreservesLiveRegisters)
{
    CheckAllocationRegisters<AllocateInYoungVertex>();
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, YoungAllocationRuntimeSlots)
{
    NativeAreaAllocator native;
    Chunk chunk {&native};
    Graph graph {&chunk};
    auto* allocation
        = Vertex::New<AllocateInYoungVertex>(&chunk, static_cast<size_t>(AllocateInYoungVertex::NUM_INPUTS));
    auto* initializer = Vertex::New<InitializeArrayWithBufferVertex>(
        &chunk,
        static_cast<size_t>(InitializeArrayWithBufferVertex::NUM_INPUTS),
        static_cast<uint32_t>(JSArray::SIZE + JSTaggedValue::TaggedTypeSize()));
    MaxCallDepthProcessor processor;
    ArkSteedState state(nullptr);
    processor.PreProcessGraph(&graph);
    processor.ProcessVertex(initializer, state);
    processor.PostProcessGraph(&graph);
    EXPECT_EQ(graph.GetMaxCallStackArgs(), 0U);
    processor.ProcessVertex(allocation, state);
    processor.PostProcessGraph(&graph);
    EXPECT_EQ(allocation->GetInputCount(), 2U);
    EXPECT_EQ(graph.GetMaxCallStackArgs(), 0U);
    auto* slow
        = Vertex::New<CallRuntimeVertex>(&chunk,
                                         std::initializer_list<ValueVertex*> {graph.GetTaggedConstant(
                                             JSTaggedValue(static_cast<int32_t>(JSObject::SIZE)).GetRawData())},
                                         RTSTUB_ID(AllocateInYoung));
    processor.ProcessVertex(slow, state);
    processor.PostProcessGraph(&graph);
    EXPECT_EQ(graph.GetMaxCallStackArgs(), slow->GetInputCount());
    EXPECT_TRUE(slow->IsCall());
    EXPECT_TRUE(slow->CanAllocate());
    // Generic flags are conservative; raw allocation has no recovery metadata.
    EXPECT_TRUE(slow->CanThrow());
    EXPECT_FALSE(slow->CanEagerDeopt());
    EXPECT_TRUE(slow->CanLazyDeopt());
    EXPECT_TRUE(slow->ReturnsRawAllocation());
    EXPECT_FALSE(slow->HasLazyDeoptFrameState());
    EXPECT_FALSE(slow->HasCatchBlock());
    EXPECT_FALSE(slow->HasExceptionLazyDeopt());
    EXPECT_FALSE(slow->CanParticipateInCSE());
    slow->VerifyInputs();
    EXPECT_EQ(ArkSteedSideEffectClassifier::Classify(slow).kind, SideEffectKind::UNKNOWN_CALL);
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, EmptyObjectConstraints)
{
    NativeAreaAllocator native;
    Chunk chunk {&native};
    auto* object
        = Vertex::New<InitializeEmptyObjectVertex>(&chunk,
                                                   static_cast<size_t>(InitializeEmptyObjectVertex::NUM_INPUTS),
                                                   static_cast<uint32_t>(JSObject::SIZE));
    CheckInitializationConstraints(object, chunk, 1);
    EXPECT_FALSE(object->IsDeferredCall()) << "empty object initialization retains a deferred call";
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, BufferedObjectConstraints)
{
    NativeAreaAllocator native;
    Chunk chunk {&native};
    CreateObjectWithBufferInfo info;
    info.objectSize = JSObject::SIZE + 2 * JSTaggedValue::TaggedTypeSize();
    info.fieldCount = 2;
    auto* fieldValues = chunk.NewArray<uint64_t>(info.fieldCount);
    fieldValues[0] = 0x8000000000000000ULL; // Raw double -0, not a tagged double.
    fieldValues[1] = JSTaggedValue::True().GetRawData();
    info.fieldValues = Span<const uint64_t>(fieldValues, info.fieldCount);
    auto* object = Vertex::New<InitializeObjectWithBufferVertex>(
        &chunk, static_cast<size_t>(InitializeObjectWithBufferVertex::NUM_INPUTS), info);
    CheckInitializationConstraints(object, chunk, 1);
    EXPECT_EQ(object->GetInfo().objectSize, info.objectSize);
    EXPECT_EQ(object->GetInfo().fieldCount, info.fieldCount);
    ASSERT_EQ(object->GetInfo().fieldValues.size(), info.fieldCount);
    EXPECT_EQ(object->GetInfo().fieldValues[0], fieldValues[0]);
    EXPECT_EQ(object->GetInfo().fieldValues[1], fieldValues[1]);
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, BufferedObjectSnapshotLifetime)
{
    NativeAreaAllocator native;
    Chunk chunk {&native};
    // Test arena ownership of already serialized bits, not broker serialization or generated code.
    const std::array<uint64_t, 9> bits = {
        0x0000000000000000ULL, // Raw double +0.
        0x8000000000000000ULL, // Raw double -0.
        0x0000000000000001ULL, // Raw smallest positive double subnormal.
        42ULL, // Raw INT.
        JSTaggedValue(42).GetRawData(),
        JSTaggedValue(1.25).GetRawData(),
        JSTaggedValue::True().GetRawData(),
        JSTaggedValue::Null().GetRawData(),
        JSTaggedValue::Undefined().GetRawData(),
    };
    constexpr uint32_t MAX_LITERAL_FIELDS = PropertyAttributes::MAX_FAST_PROPS_CAPACITY - JSObject::SIZE;
    for (uint32_t fieldCount: {0U, 2U, 32U, 33U, 64U, MAX_LITERAL_FIELDS}) {
        InitializeObjectWithBufferVertex* object = nullptr;
        {
            CreateObjectWithBufferInfo info;
            // Spare slots are independent of the number of initialized properties.
            info.objectSize = JSObject::SIZE + std::max(fieldCount, 40U) * JSTaggedValue::TaggedTypeSize();
            info.fieldCount = fieldCount;
            auto* fieldValues = fieldCount == 0 ? nullptr : chunk.NewArray<uint64_t>(fieldCount);
            for (uint32_t i = 0; i < fieldCount; ++i) {
                fieldValues[i] = bits[i % bits.size()];
            }
            info.fieldValues = Span<const uint64_t>(fieldValues, fieldCount);
            object = Vertex::New<InitializeObjectWithBufferVertex>(
                &chunk, static_cast<size_t>(InitializeObjectWithBufferVertex::NUM_INPUTS), info);
        }
        // Force an arena expansion after the local snapshot has gone out of scope.
        auto* extra = chunk.NewArray<uint64_t>(64U * 1024U);
        std::fill_n(extra, 64U * 1024U, 0);
        const auto& saved = object->GetInfo();
        EXPECT_EQ(saved.fieldCount, fieldCount);
        ASSERT_EQ(saved.fieldValues.size(), fieldCount);
        EXPECT_EQ(saved.objectSize, JSObject::SIZE + std::max(fieldCount, 40U) * JSTaggedValue::TaggedTypeSize());
        for (uint32_t i = 0; i < fieldCount; ++i) {
            EXPECT_EQ(saved.fieldValues[i], bits[i % bits.size()]) << "field " << i;
        }
        CheckInitializationConstraints(object, chunk, 1);
    }
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, EmptyArrayConstraints)
{
    NativeAreaAllocator native;
    Chunk chunk {&native};
    auto* array = Vertex::New<InitializeEmptyArrayVertex>(
        &chunk,
        static_cast<size_t>(InitializeEmptyArrayVertex::NUM_INPUTS),
        static_cast<uint32_t>(JSArray::SIZE + JSTaggedValue::TaggedTypeSize()));
    CheckInitializationConstraints(array, chunk, 2);
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, BufferedArrayConstraints)
{
    NativeAreaAllocator native;
    Chunk chunk {&native};
    auto* array = Vertex::New<InitializeArrayWithBufferVertex>(
        &chunk,
        static_cast<size_t>(InitializeArrayWithBufferVertex::NUM_INPUTS),
        static_cast<uint32_t>(JSArray::SIZE + JSTaggedValue::TaggedTypeSize()));
    CheckInitializationConstraints(array, chunk, 2);
    EXPECT_EQ(InitializeArrayWithBufferVertex::INPUT_TYPES[InitializeArrayWithBufferVertex::LITERAL_INDEX],
              ValueRepresentation::TAGGED);
    EXPECT_EQ(array->GetInputCount(), 3U);
    EXPECT_EQ(array->GetObjectSize(), JSArray::SIZE + JSTaggedValue::TaggedTypeSize());
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, SpillSlotsPressureAndPoolAccounting)
{
    RunSpillPressureAndPoolAccounting();
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, SpillSlotsReuseNewestFreedSlot)
{
    RunSpillSlotReusePolicy();
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, SpillSlotsRejectOverlappingLiveRange)
{
    RunSpillSlotOverlapRejection();
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, SpillSlotsTaggedAndUntaggedPoolsSeparate)
{
    RunTaggedAndUntaggedPoolsAreIndependent();
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, SpillSlotsDoubleAndPointerSlotsSeparate)
{
    RunDoubleAndPointerSlotsDoNotMix();
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, SpillSlotsReuseStackSlotsOption)
{
    RunReuseStackSlotsOption();
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, SpillSlotsNegativeControlShiftsFreedPosition)
{
    RunSpillSlotNegativeControl();
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, RawYoungAllocationFastConstraints)
{
    NativeAreaAllocator native;
    Chunk chunk {&native};
    auto *allocation = Vertex::New<AllocateRawInYoungFastVertex>(
        &chunk, static_cast<size_t>(AllocateRawInYoungFastVertex::NUM_INPUTS));
    CheckAllocationConstraints(allocation, chunk, 1);
    EXPECT_FALSE(allocation->IsCall()) << "inline allocation attempt unexpectedly became a call";
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, InitializeUnmappedArgumentsElementsConstraints)
{
    NativeAreaAllocator native;
    Chunk chunk {&native};
    auto *initialize = Vertex::New<InitializeUnmappedArgumentsElementsVertex>(
        &chunk, static_cast<size_t>(InitializeUnmappedArgumentsElementsVertex::NUM_INPUTS));
    initialize->SetRegallocInfo(chunk.New<RegallocValueVertexInfo>(
        &chunk, initialize->GetInputCount(), initialize->GetMachineRepresentation()));
    for (uint32_t i = 0; i < initialize->GetInputCount(); ++i) {
        initialize->SetInput(i, NewConstantProducer(
            InitializeUnmappedArgumentsElementsVertex::INPUT_TYPES[i], chunk));
    }
    initialize->SetValueLocationConstraints();
    EXPECT_TRUE(UnallocatedState::Cast(initialize->Result().GetOperand()).GetExtendedPolicy() ==
                Policy::MUST_HAVE_REGISTER);
    for (unsigned i = 0; i < initialize->GetInputCount(); ++i) {
        auto operand = UnallocatedState::Cast(initialize->GetInputLocation(i)->GetOperand());
        EXPECT_TRUE(operand.GetExtendedPolicy() == Policy::MUST_HAVE_REGISTER && !operand.IsUsedAtStart())
            << "unmapped arguments elements input does not use ordinary UseRegister";
    }
    EXPECT_TRUE(initialize->GetNumTemporariesNeeded<ArkSteedRegister>() == 1 &&
                initialize->GetRegallocInfo()->GetRequiredSpecificTemporaries<ArkSteedRegister>().IsEmpty());
    EXPECT_FALSE(initialize->IsCall()) << "unmapped arguments elements initialization unexpectedly became a call";
    EXPECT_FALSE(initialize->IsDeferredCall())
        << "read-only elements HClass initialization unexpectedly retained a deferred barrier";
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, InitializeUnmappedArgumentsConstraints)
{
    NativeAreaAllocator native;
    Chunk chunk {&native};
    auto *initialize = Vertex::New<InitializeUnmappedArgumentsVertex>(
        &chunk, static_cast<size_t>(InitializeUnmappedArgumentsVertex::NUM_INPUTS));
    initialize->SetRegallocInfo(chunk.New<RegallocValueVertexInfo>(
        &chunk, initialize->GetInputCount(), initialize->GetMachineRepresentation()));
    for (uint32_t i = 0; i < initialize->GetInputCount(); ++i) {
        initialize->SetInput(i, NewConstantProducer(
            InitializeUnmappedArgumentsVertex::INPUT_TYPES[i], chunk));
    }
    initialize->SetValueLocationConstraints();
    EXPECT_TRUE(UnallocatedState::Cast(initialize->Result().GetOperand()).GetExtendedPolicy() ==
                Policy::MUST_HAVE_REGISTER);
    for (unsigned i = 0; i < initialize->GetInputCount(); ++i) {
        auto operand = UnallocatedState::Cast(initialize->GetInputLocation(i)->GetOperand());
        EXPECT_TRUE(operand.GetExtendedPolicy() == Policy::MUST_HAVE_REGISTER && !operand.IsUsedAtStart())
            << "unmapped arguments input does not use ordinary UseRegister";
    }
    EXPECT_TRUE(initialize->GetNumTemporariesNeeded<ArkSteedRegister>() == 3 &&
                initialize->GetRegallocInfo()->GetRequiredSpecificTemporaries<ArkSteedRegister>().IsEmpty());
    EXPECT_FALSE(initialize->CanAllocate()) << "unmapped arguments initialization still owns allocation";
    EXPECT_FALSE(initialize->IsCall()) << "unmapped arguments initialization unexpectedly became a call";
    EXPECT_TRUE(initialize->IsDeferredCall())
        << "unmapped arguments initialization must expose its deferred barriers";
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, UnmappedArgumentsTemporariesNeverAliasInputs)
{
    RunUnmappedArgumentsTemporariesNeverAliasInputs();
}

} // namespace panda::ecmascript::arksteed
