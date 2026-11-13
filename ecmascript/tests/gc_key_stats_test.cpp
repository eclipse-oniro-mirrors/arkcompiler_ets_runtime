/*
 * Copyright (c) 2025-2026 Huawei Device Co., Ltd.
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

#include <chrono>
#include <thread>

#include "assembler/assembly-emitter.h"
#include "assembler/assembly-parser.h"
#include "ecmascript/builtins/builtins_ark_tools.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/mem/full_gc.h"
#include "ecmascript/object_factory-inl.h"
#include "ecmascript/mem/concurrent_marker.h"
#include "ecmascript/mem/partial_gc.h"
#include "ecmascript/mem/gc_key_stats.h"
#include "ecmascript/mem/gc_stats.h"
#include "ecmascript/mem/long_gc_stats.h"
#include "ecmascript/platform/dfx_hisys_event.h"
#include "ecmascript/js_runtime_options.h"
#include "ecmascript/tests/ecma_test_common.h"
#include "ecmascript/mem/mem_controller.h"
#include "ecmascript/runtime.h"

using namespace panda;

using namespace panda::ecmascript;

namespace panda::test {

class GCKeyStatsTest : public BaseTestWithScope<false> {
public:
    void SetUp() override
    {
        JSRuntimeOptions options;
        instance = JSNApi::CreateEcmaVM(options);
        ASSERT_TRUE(instance != nullptr) << "Cannot create EcmaVM";
        thread = instance->GetJSThread();
        thread->ManagedCodeBegin();
        scope = new EcmaHandleScope(thread);
    }
};

HWTEST_F_L0(GCKeyStatsTest, ShouldSkipLongGCReportTest001)
{
    EXPECT_TRUE(GCKeyStats::ShouldSkipLongGCReport(true, GCReason::OTHER));
    EXPECT_TRUE(GCKeyStats::ShouldSkipLongGCReport(false, GCReason::IDLE));
    EXPECT_TRUE(GCKeyStats::ShouldSkipLongGCReport(false, GCReason::IDLE_NATIVE));
    EXPECT_TRUE(GCKeyStats::ShouldSkipLongGCReport(true, GCReason::IDLE));
    EXPECT_FALSE(GCKeyStats::ShouldSkipLongGCReport(false, GCReason::OTHER));
    EXPECT_FALSE(GCKeyStats::ShouldSkipLongGCReport(false, GCReason::TRIGGER_BY_JS));
}

HWTEST_F_L0(GCKeyStatsTest, ShouldReportLongGCTest001)
{
    EXPECT_TRUE(GCKeyStats::ShouldReportLongGC(true,
        static_cast<float>(GCKeyStats::GC_SENSITIVE_LONG_TIME) + 0.1f));
    EXPECT_FALSE(GCKeyStats::ShouldReportLongGC(true,
        static_cast<float>(GCKeyStats::GC_SENSITIVE_LONG_TIME)));
    EXPECT_TRUE(GCKeyStats::ShouldReportLongGC(false,
        static_cast<float>(GCKeyStats::GC_NOT_SENSITIVE_LONG_TIME) + 0.1f));
    EXPECT_FALSE(GCKeyStats::ShouldReportLongGC(false,
        static_cast<float>(GCKeyStats::GC_NOT_SENSITIVE_LONG_TIME)));
}

HWTEST_F_L0(GCKeyStatsTest, GetAppArkTSModeTest001)
{
    EcmaVM *vm = thread->GetEcmaVM();
    JSRuntimeOptions &options = vm->GetJSOptions();
    options.SetArkTSMode(std::string(LANGUAGE_DYNAMIC));
    ASSERT_TRUE(Runtime::HasInstance());
    EcmaVM *mainVm = Runtime::GetInstance()->GetMainThread()->GetEcmaVM();
    ASSERT_TRUE(mainVm != nullptr);
    EXPECT_EQ(mainVm->GetArkTSMode(), ArkTSMode::DYNAMIC);
}

HWTEST_F_L0(GCKeyStatsTest, GetAppArkTSModeTest002)
{
    EcmaVM *vm = thread->GetEcmaVM();
    JSRuntimeOptions &options = vm->GetJSOptions();
    options.SetArkTSMode(std::string(LANGUAGE_STATIC));
    ASSERT_TRUE(Runtime::HasInstance());
    EcmaVM *mainVm = Runtime::GetInstance()->GetMainThread()->GetEcmaVM();
    ASSERT_TRUE(mainVm != nullptr);
    EXPECT_EQ(mainVm->GetArkTSMode(), ArkTSMode::STATIC);
}

HWTEST_F_L0(GCKeyStatsTest, GetAppArkTSModeTest003)
{
    EcmaVM *vm = thread->GetEcmaVM();
    JSRuntimeOptions &options = vm->GetJSOptions();
    options.SetArkTSMode(std::string(LANGUAGE_HYBRID));
    ASSERT_TRUE(Runtime::HasInstance());
    EcmaVM *mainVm = Runtime::GetInstance()->GetMainThread()->GetEcmaVM();
    ASSERT_TRUE(mainVm != nullptr);
    EXPECT_EQ(mainVm->GetArkTSMode(), ArkTSMode::HYBRID);
}
}  // namespace panda::test
