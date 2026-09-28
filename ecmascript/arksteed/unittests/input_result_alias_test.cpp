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

#include "ecmascript/arksteed/arksteed_graph.h"
#include "ecmascript/arksteed/arksteed_regalloc.h"
#include "ecmascript/js_object.h"
#include "ecmascript/mem/native_area_allocator.h"
#include "ecmascript/tests/test_helper.h"

namespace panda::ecmascript::arksteed {

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

    template<class VertexT>
    static void CheckAllocationConstraints(VertexT* vertex, Chunk& chunk, unsigned temporaries)
    {
        vertex->SetRegallocInfo(chunk.New<RegallocValueVertexInfo>(
            &chunk, vertex->GetInputCount(), vertex->GetMachineRepresentation()));
        vertex->SetValueLocationConstraints();
        EXPECT_TRUE(UnallocatedState::Cast(vertex->Result().GetOperand()).GetExtendedPolicy() ==
                    Policy::MUST_HAVE_REGISTER)
            << "allocation result is bound to the call ABI";
        for (unsigned i = 0; i < vertex->GetInputCount(); ++i) {
            auto operand = UnallocatedState::Cast(vertex->GetInputLocation(i)->GetOperand());
            EXPECT_TRUE(operand.GetExtendedPolicy() == Policy::MUST_HAVE_REGISTER && !operand.IsUsedAtStart())
                << "allocation input does not use ordinary UseRegister";
        }
        EXPECT_TRUE(
            vertex->template GetNumTemporariesNeeded<ArkSteedRegister>() == temporaries &&
            vertex->GetRegallocInfo()->template GetRequiredSpecificTemporaries<ArkSteedRegister>().IsEmpty())
            << "allocation retains a glue-copy temporary";
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

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, EmptyObjectConstraints)
{
    NativeAreaAllocator native;
    Chunk chunk {&native};
    auto* object = Vertex::New<CreateEmptyObjectVertex>(
        &chunk, static_cast<size_t>(CreateEmptyObjectVertex::NUM_INPUTS), static_cast<uint32_t>(JSObject::SIZE));
    CheckAllocationConstraints(object, chunk, 2);
    EXPECT_FALSE(object->IsDeferredCall()) << "empty object initialization retains a deferred call";
}

HWTEST_F_L0(ArkSteedRegisterAllocatorTest, EmptyArrayConstraints)
{
    NativeAreaAllocator native;
    Chunk chunk {&native};
    auto* array =
        Vertex::New<AllocateJSArrayVertex>(&chunk, static_cast<size_t>(AllocateJSArrayVertex::NUM_INPUTS));
    CheckAllocationConstraints(array, chunk, 2);
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

} // namespace panda::ecmascript::arksteed
