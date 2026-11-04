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

namespace panda::ecmascript::arksteed {


/**
 * @tc.name: ArkSteedDceTest.ClassifyReadsThePositiveRemovalProperty
 * @tc.desc: Real arithmetic, bitwise, comparison, conversion and merge values are
 *           removable. Side-effect freedom and CSE eligibility alone do not
 *           authorize removal; registry values and unknown opcodes stay required.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, ClassifyReadsThePositiveRemovalProperty)
{
    CheckRemovableArithmetic();
    CheckRemovableBitwiseAndComparisons();
    CheckRemovableConversionsAndMerge();
    CheckSideEffectsAndCseDoNotAuthorizeRemoval();
    CheckRegistryAndStructuralValuesRemainRequired();
    CheckUnknownOpcodesIgnoreRemovalAuthorization();
}

/**
 * @tc.name: ArkSteedDceTest.RemovablePolicyControlsConstructEveryNamedClass
 * @tc.desc: Constructed calls, writes, barriers, allocations, exceptional
 *           operations, address loads, safepoints and division stay required.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, RemovablePolicyControlsConstructEveryNamedClass)
{
    auto *address = Vertex::New<Int64ConstantVertex>(&chunk_, SAMPLE_VERTEX_ID, SAMPLE_INT32_VALUE);
    auto *tagged = NewTaggedSample(JSTaggedValue::Undefined());
    ASSERT_NE(address, nullptr);
    ASSERT_NE(tagged, nullptr);
    CheckCallsRemainRequired(address, tagged);
    CheckWritesAndAllocationsRemainRequired(address, tagged);
    CheckExceptionalAndMemoryOperationsRemainRequired(address, tagged);
}

/**
 * @tc.name: ArkSteedDceTest.DependenciesEnumerateDirectEagerLazyAndCatchClasses
 * @tc.desc: Dependencies retain direct, eager, lazy and catch input order before
 *           allocation; absent optional lazy state contributes no extra values.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, DependenciesEnumerateDirectEagerLazyAndCatchClasses)
{
    auto *direct = NewTaggedSample(JSTaggedValue::Undefined());
    auto *frame = NewTaggedSample(JSTaggedValue::Null());
    ASSERT_NE(direct, nullptr);
    ASSERT_NE(frame, nullptr);
    CheckCombinedLazyAndCatchDependencies(direct, frame);
    CheckCombinedEagerDependencies(frame);
    CheckAbsentLazyFrameContributesNoDependencies(direct);
}

/**
 * @tc.name: ArkSteedDceTest.DependenciesEnumeratePhiIncomingValues
 * @tc.desc: A live Phi reports incoming values in predecessor order.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, DependenciesEnumeratePhiIncomingValues)
{
    auto *first = NewTaggedSample(JSTaggedValue::Undefined());
    auto *second = NewTaggedSample(JSTaggedValue::Null());
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    auto *phi = NewVertex<PhiVertex>({first, second}, VirtualRegister(FIRST_VREG));
    ExpectDependencies(phi, {first, second});
}

/**
 * @tc.name: ArkSteedDceTest.DependenciesIsolateEachClassWithDistinguishableProducers
 * @tc.desc: Direct-only, eager-only, lazy-only and catch-only consumers each
 *           report their own distinguishable producer with no extra visits.
 * @tc.type: FUNC
 */
HWTEST_F_L0(ArkSteedDceTest, DependenciesIsolateEachClassWithDistinguishableProducers)
{
    auto *direct = NewTaggedSample(JSTaggedValue::Undefined());
    auto *eager = NewTaggedSample(JSTaggedValue::Null());
    auto *lazy = NewTaggedSample(JSTaggedValue::False());
    auto *caught = NewTaggedSample(JSTaggedValue::True());
    ASSERT_NE(direct, nullptr);
    ASSERT_NE(eager, nullptr);
    ASSERT_NE(lazy, nullptr);
    ASSERT_NE(caught, nullptr);
    ExpectDependencies(NewRuntimeConsumer({direct}), {direct});
    CheckIsolatedEagerDependency(eager);
    CheckIsolatedLazyDependency(lazy);
    CheckIsolatedCatchDependency(caught);
}

}  // namespace panda::ecmascript::arksteed
