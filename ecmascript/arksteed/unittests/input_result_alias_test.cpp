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
            EXPECT_TRUE(f.Allocate() == (endCount == 0 ? f.first : f.second))
                << "ordinary result could not reuse a USED_AT_START input";
            EXPECT_TRUE(f.result->GetInput(1)->GetRegallocInfo()->IsSpilled())
                << "reusing a consumed input lost its later live value";
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
            // Exercise the unblocked-free fast path as well as pressure paths.
            f.Registers().ClearBlocked();
            f.HintFirst();
            EXPECT_TRUE(f.Allocate() != f.first) << "free-list hint bypassed result exclusion";
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
            EXPECT_TRUE(f.Allocate() == f.first) << "UseAny acquired a new result constraint";
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

} // namespace panda::ecmascript::arksteed
