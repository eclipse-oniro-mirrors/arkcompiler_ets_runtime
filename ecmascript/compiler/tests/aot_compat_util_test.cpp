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

#include <cstdint>
#include <string>

#include "ecmascript/compiler/aot_compat_util.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {
class AotCompatUtilTest : public testing::Test {
public:
    void SetUp() override
    {
        AotCompatUtil::ResetReservedFlag();
    }

    void TearDown() override
    {
    }
};

/**
 * @tc.name: EstimateGateCount_ZeroBytecode
 * @tc.desc: Test EstimateGateCount returns 0 when bytecode count is 0
 * @tc.type: FUNC
 */
HWTEST_F_L0(AotCompatUtilTest, EstimateGateCount_ZeroBytecode)
{
    EXPECT_EQ(AotCompatUtil::EstimateGateCount(0), 0U);
}

/**
 * @tc.name: EstimateGateCount_SingleBytecode
 * @tc.desc: Test EstimateGateCount expands one bytecode by the gate factor
 * @tc.type: FUNC
 */
HWTEST_F_L0(AotCompatUtilTest, EstimateGateCount_SingleBytecode)
{
    EXPECT_EQ(AotCompatUtil::EstimateGateCount(1), 4U);  // 4: rough average gate expansion factor
}

/**
 * @tc.name: EstimateGateCount_NormalCases
 * @tc.desc: Test EstimateGateCount scales linearly with bytecode count
 * @tc.type: FUNC
 */
HWTEST_F_L0(AotCompatUtilTest, EstimateGateCount_NormalCases)
{
    EXPECT_EQ(AotCompatUtil::EstimateGateCount(10), 40U);      // 10 * 4
    EXPECT_EQ(AotCompatUtil::EstimateGateCount(100), 400U);    // 100 * 4
    EXPECT_EQ(AotCompatUtil::EstimateGateCount(1000), 4000U);  // 1000 * 4
}

/**
 * @tc.name: ShouldSkipMethod_GreaterThanLimit
 * @tc.desc: Test ShouldSkipMethod returns true when gate count exceeds the limit
 * @tc.type: FUNC
 */
HWTEST_F_L0(AotCompatUtilTest, ShouldSkipMethod_GreaterThanLimit)
{
    EXPECT_TRUE(AotCompatUtil::ShouldSkipMethod(101, 100));
    EXPECT_TRUE(AotCompatUtil::ShouldSkipMethod(1000, 100));
}

/**
 * @tc.name: ShouldSkipMethod_EqualToLimit
 * @tc.desc: Test ShouldSkipMethod returns false on the boundary (equal)
 * @tc.type: FUNC
 */
HWTEST_F_L0(AotCompatUtilTest, ShouldSkipMethod_EqualToLimit)
{
    EXPECT_FALSE(AotCompatUtil::ShouldSkipMethod(100, 100));
    EXPECT_FALSE(AotCompatUtil::ShouldSkipMethod(0, 0));
}

/**
 * @tc.name: ShouldSkipMethod_LessThanLimit
 * @tc.desc: Test ShouldSkipMethod returns false when gate count is below the limit
 * @tc.type: FUNC
 */
HWTEST_F_L0(AotCompatUtilTest, ShouldSkipMethod_LessThanLimit)
{
    EXPECT_FALSE(AotCompatUtil::ShouldSkipMethod(0, 100));
    EXPECT_FALSE(AotCompatUtil::ShouldSkipMethod(99, 100));
}

/**
 * @tc.name: BuildAotSummaryTag_Normal
 * @tc.desc: Test BuildAotSummaryTag produces "record@count" format
 * @tc.type: FUNC
 */
HWTEST_F_L0(AotCompatUtilTest, BuildAotSummaryTag_Normal)
{
    EXPECT_EQ(AotCompatUtil::BuildAotSummaryTag("app", 5), "app@5");
    EXPECT_EQ(AotCompatUtil::BuildAotSummaryTag("com.test.bundle", 100), "com.test.bundle@100");
}

/**
 * @tc.name: BuildAotSummaryTag_EmptyRecordName
 * @tc.desc: Test BuildAotSummaryTag with empty record name and zero method count
 * @tc.type: FUNC
 */
HWTEST_F_L0(AotCompatUtilTest, BuildAotSummaryTag_EmptyRecordName)
{
    EXPECT_EQ(AotCompatUtil::BuildAotSummaryTag("", 0), "@0");
}

/**
 * @tc.name: ReservedFlag_IsFalseInitially
 * @tc.desc: Test IsReservedFlagSet returns false after reset in SetUp
 * @tc.type: FUNC
 */
HWTEST_F_L0(AotCompatUtilTest, ReservedFlag_IsFalseInitially)
{
    EXPECT_FALSE(AotCompatUtil::IsReservedFlagSet());
}

/**
 * @tc.name: ReservedFlag_ResetKeepsFalse
 * @tc.desc: Test ResetReservedFlag keeps the flag false across repeated resets
 * @tc.type: FUNC
 */
HWTEST_F_L0(AotCompatUtilTest, ReservedFlag_ResetKeepsFalse)
{
    AotCompatUtil::ResetReservedFlag();
    EXPECT_FALSE(AotCompatUtil::IsReservedFlagSet());
    AotCompatUtil::ResetReservedFlag();
    EXPECT_FALSE(AotCompatUtil::IsReservedFlagSet());
}
}  // namespace panda::test
