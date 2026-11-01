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

#include "ecmascript/tests/test_helper.h"

#ifdef PANDA_JS_ETS_HYBRID_MODE
#include "ecmascript/cross_vm/cross_vm_operator.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/js_runtime_options.h"
#include "ecmascript/napi/include/jsnapi.h"
#include "hybrid/sts_vm_interface.h"
#endif

using namespace panda::ecmascript;

namespace panda::test {

#ifdef PANDA_JS_ETS_HYBRID_MODE
class STSVMInterfaceMock : public arkplatform::STSVMInterface {
public:
    void MarkFromObject([[maybe_unused]] void *obj) override {}
    void OnVMAttach() override {}
    void OnVMDetach() override {}
    bool StartXGCBarrier([[maybe_unused]] const NoWorkPred &func) override { return true; }
    bool WaitForConcurrentMark([[maybe_unused]] const NoWorkPred &func) override { return true; }
    void RemarkStartBarrier() override {}
    bool WaitForRemark([[maybe_unused]] const NoWorkPred &func) override { return true; }
    void FinishXGCBarrier() override {}
    bool TriggerXGC() override { return false; }
    void NotifyWaiters() override {}
    bool GetStaticFrameInfo([[maybe_unused]] const void *frame,
                            [[maybe_unused]] arkplatform::HybridFrameInfo &frameInfo) override { return false; }
    bool TriggerXGCAndWait() override { return false; }
    void EtsForceFullGC() override {}
    void SuspendEtsThreads() override {}
    void ResumeEtsThreads() override {}
    std::vector<arkplatform::NodeInfo> GetEtsVMRoots() override { return {}; }
    void GetEtsNodeEdges([[maybe_unused]] uint64_t etsAddr,
                         [[maybe_unused]] std::vector<arkplatform::EdgeInfo> &edges,
                         [[maybe_unused]] bool isSimplify,
                         [[maybe_unused]] bool captureNumericValue) override {}
    arkplatform::NodeInfo GetEtsNodeInfo([[maybe_unused]] uint64_t etsAddr) override { return {}; }
    std::vector<arkplatform::NodeInfo> GetAllEtsObjects() override { return {}; }
    void IterateEtsObjects([[maybe_unused]] const std::function<void(uint64_t)> &callback) override {}
    void GetXRefMaps([[maybe_unused]] uintptr_t ecmaVM,
                     [[maybe_unused]] XRefMap &jsToEts,
                     [[maybe_unused]] XRefMap &etsToJs) override {}
    bool AttachCurrentThread() override { return true; }
    bool DetachCurrentThread() override { return true; }
    bool IsCurrentThreadAttached() override { return true; }
    bool ExecuteHeapDump([[maybe_unused]] const common::dump::DumpRequest &request,
                         [[maybe_unused]] arkplatform::EcmaVMInterface *ecmaInterface,
                         [[maybe_unused]] bool dumpStaticHeap) override { return false; }
    bool UnionStackIsEmpty([[maybe_unused]] bool *isEmpty) override { return true; }
    bool ForEachFrameInUnionStack(
        [[maybe_unused]] const std::function<void(const void *frame, bool isStaticFrame)> &callback) override
    {
        return true;
    }
};
#endif

class CrossVMOperatorTest : public BaseTestWithScope<false> {};

#ifdef PANDA_JS_ETS_HYBRID_MODE
HWTEST_F_L0(CrossVMOperatorTest, GetArkTSModeTest)
{
    EcmaVM *vm = thread->GetEcmaVM();
    auto stsVMInterface = std::make_unique<STSVMInterfaceMock>();
    void *ecmaVMInterface = nullptr;
    CrossVMOperator::DoHandshake(vm, stsVMInterface.get(), &ecmaVMInterface);

    auto *ecmaIface = vm->GetCrossVMOperator()->GetEcmaVMInterface();
    ASSERT_TRUE(ecmaIface != nullptr);

    JSRuntimeOptions &options = vm->GetJSOptions();
    options.SetArkTSMode(std::string(LANGUAGE_DYNAMIC));
    EXPECT_EQ(ecmaIface->GetArkTSMode(), "dynamic");

    options.SetArkTSMode(std::string(LANGUAGE_STATIC));
    EXPECT_EQ(ecmaIface->GetArkTSMode(), "static");

    options.SetArkTSMode(std::string(LANGUAGE_HYBRID));
    EXPECT_EQ(ecmaIface->GetArkTSMode(), "hybrid");
}
#endif  // PANDA_JS_ETS_HYBRID_MODE
}  // namespace panda::test
