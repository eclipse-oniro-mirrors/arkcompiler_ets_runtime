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

#include "ecmascript/jit/jit_compat_util.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {

class JitCompatUtilTest : public BaseTestWithScope<false> {
};

/**
 * @tc.name: CalcHotnessBias_GreaterThanThreshold
 * @tc.desc: Test CalcHotnessBias when hotness exceeds threshold
 * @tc.type: FUNC
 */
HWTEST_F_L0(JitCompatUtilTest, CalcHotnessBias_GreaterThanThreshold)
{
    EXPECT_EQ(JitCompatUtil::CalcHotnessBias(100, 30), 70U);
    EXPECT_EQ(JitCompatUtil::CalcHotnessBias(31, 30), 1U);
}

/**
 * @tc.name: CalcHotnessBias_EqualToThreshold
 * @tc.desc: Test CalcHotnessBias when hotness equals threshold
 * @tc.type: FUNC
 */
HWTEST_F_L0(JitCompatUtilTest, CalcHotnessBias_EqualToThreshold)
{
    EXPECT_EQ(JitCompatUtil::CalcHotnessBias(30, 30), 0U);
}

/**
 * @tc.name: CalcHotnessBias_LessThanThreshold
 * @tc.desc: Test CalcHotnessBias when hotness is below threshold
 * @tc.type: FUNC
 */
HWTEST_F_L0(JitCompatUtilTest, CalcHotnessBias_LessThanThreshold)
{
    EXPECT_EQ(JitCompatUtil::CalcHotnessBias(0, 30), 0U);
    EXPECT_EQ(JitCompatUtil::CalcHotnessBias(29, 30), 0U);
}

/**
 * @tc.name: IsHotnessOverflow_GreaterThanThreshold
 * @tc.desc: Test IsHotnessOverflow when hotness exceeds threshold
 * @tc.type: FUNC
 */
HWTEST_F_L0(JitCompatUtilTest, IsHotnessOverflow_GreaterThanThreshold)
{
    EXPECT_TRUE(JitCompatUtil::IsHotnessOverflow(31, 30));
    EXPECT_TRUE(JitCompatUtil::IsHotnessOverflow(100, 30));
}

/**
 * @tc.name: IsHotnessOverflow_EqualToThreshold
 * @tc.desc: Test IsHotnessOverflow when hotness equals threshold (boundary)
 * @tc.type: FUNC
 */
HWTEST_F_L0(JitCompatUtilTest, IsHotnessOverflow_EqualToThreshold)
{
    EXPECT_FALSE(JitCompatUtil::IsHotnessOverflow(30, 30));
}

/**
 * @tc.name: IsHotnessOverflow_LessThanThreshold
 * @tc.desc: Test IsHotnessOverflow when hotness is below threshold
 * @tc.type: FUNC
 */
HWTEST_F_L0(JitCompatUtilTest, IsHotnessOverflow_LessThanThreshold)
{
    EXPECT_FALSE(JitCompatUtil::IsHotnessOverflow(0, 30));
    EXPECT_FALSE(JitCompatUtil::IsHotnessOverflow(29, 30));
}

/**
 * @tc.name: FormatJitTag_Normal
 * @tc.desc: Test FormatJitTag produces "method:hotness" format
 * @tc.type: FUNC
 */
HWTEST_F_L0(JitCompatUtilTest, FormatJitTag_Normal)
{
    EXPECT_EQ(JitCompatUtil::FormatJitTag("factorial", 42), "factorial:42");
    EXPECT_EQ(JitCompatUtil::FormatJitTag("fib", 0), "fib:0");
}

/**
 * @tc.name: FormatJitTag_EmptyMethodName
 * @tc.desc: Test FormatJitTag with empty method name
 * @tc.type: FUNC
 */
HWTEST_F_L0(JitCompatUtilTest, FormatJitTag_EmptyMethodName)
{
    EXPECT_EQ(JitCompatUtil::FormatJitTag("", 10), ":10");
}

/**
 * @tc.name: ReservedCounter_ResetAndGetInt64
 * @tc.desc: Test ResetReservedCounter zeroes the counter and GetReservedCounter reads it
 * @tc.type: FUNC
 */
HWTEST_F_L0(JitCompatUtilTest, ReservedCounter_ResetAndGet)
{
    JitCompatUtil::ResetReservedCounter();
    EXPECT_EQ(JitCompatUtil::GetReservedCounter(), 0U);
}

/**
 * @tc.name: ReservedCounter_IsZeroInitially
 * @tc.desc: Test GetReservedCounter returns 0 after reset at start
 * @tc.type: FUNC
 */
HWTEST_F_L0(JitCompatUtilTest, ReservedCounter_IsZeroInitially)
{
    JitCompatUtil::ResetReservedCounter();
    EXPECT_EQ(JitCompatUtil::GetReservedCounter(), 0U);
    JitCompatUtil::ResetReservedCounter();
    EXPECT_EQ(JitCompatUtil::GetReservedCounter(), 0U);
}
}  // namespace panda::test
