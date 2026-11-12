/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#include "ecmascript/platform/backtrace.h"
#include "ecmascript/tests/test_helper.h"

using namespace panda::ecmascript;

namespace panda::test {
class BacktraceTest : public ::testing::Test {
public:
    static void SetUpTestCase()
    {
        GTEST_LOG_(INFO) << "SetUpTestCase";
    }

    static void TearDownTestCase()
    {
        GTEST_LOG_(INFO) << "TearDownCase";
    }

    void SetUp() override
    {
        TestHelper::CreateEcmaVMWithScope(instance, thread, scope);
        instance->SetEnableForceGC(false);
    }

    void TearDown() override
    {
        TestHelper::DestroyEcmaVMWithScope(instance, scope);
    }

    EcmaVM *instance{nullptr};
    EcmaHandleScope *scope{nullptr};
    JSThread *thread{nullptr};
};

HWTEST_F_L0(BacktraceTest, BacktraceTest1)
{
    std::ostringstream stack;
    bool enableCache = true;
    Backtrace(stack, enableCache);
#if defined(ENABLE_BACKTRACE_LOCAL) || defined(ENABLE_UNWINDER)
    // OHOS builds with the local backtrace or unwinder enabled fill the stack.
    EXPECT_TRUE(!stack.str().empty());
#elif defined(PANDA_TARGET_OHOS)
    // OHOS standalone builds (e.g. unittest running with qemu) compile
    // Backtrace() without backtrace support, so it is a no-op and leaves the
    // stream empty.
    EXPECT_TRUE(stack.str().empty());
#else
    // Linux host builds use the execinfo-based backtrace.
    EXPECT_TRUE(!stack.str().empty());
#endif
}

HWTEST_F_L0(BacktraceTest, BacktraceTest2)
{
    std::ostringstream stack;
    Backtrace(stack);
#if defined(ENABLE_BACKTRACE_LOCAL) || defined(ENABLE_UNWINDER)
    // OHOS builds with the local backtrace or unwinder enabled fill the stack.
    EXPECT_TRUE(!stack.str().empty());
#elif defined(PANDA_TARGET_OHOS)
    // OHOS standalone builds (e.g. unittest running with qemu) compile
    // Backtrace() without backtrace support, so it is a no-op and leaves the
    // stream empty.
    EXPECT_TRUE(stack.str().empty());
#else
    // Linux host builds use the execinfo-based backtrace.
    EXPECT_TRUE(!stack.str().empty());
#endif
}

HWTEST_F_L0(BacktraceTest, BacktraceHybrid1)
{
    auto vm = thread->GetEcmaVM();
    int size = BacktraceHybrid(vm->GetPcVectorData());
#if defined(ENABLE_EXCEPTION_BACKTRACE) && defined(ENABLE_BACKTRACE_LOCAL)
    EXPECT_TRUE(size != 0);
#else
    EXPECT_TRUE(size == 0);
#endif
}
}