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

#ifndef ECMASCRIPT_ARKSTEED_PGO_DEPENDENCY_RECORDER_H
#define ECMASCRIPT_ARKSTEED_PGO_DEPENDENCY_RECORDER_H

#include "ecmascript/arksteed/arksteed_heap_broker.h"
#include "ecmascript/arksteed/arksteed_pgo_access_info.h"
#include "ecmascript/compiler/jit_compilation_env.h"

namespace panda::ecmascript::arksteed {

class ArkSteedPGODependencyRecorder {
public:
    ArkSteedPGODependencyRecorder(JitCompilationEnv *env, const ArkSteedHeapBroker *broker) : env_(env), broker_(broker)
    {}

    bool Install(PropertyAccessSet *access) const
    {
        if (broker_ == nullptr) {
            return false;
        }
        if (access == nullptr || access->caseCount == 0) {
            return false;
        }
        for (uint32_t i = 0; i < access->caseCount; ++i) {
            if (!Validate(access->cases[i])) {
                return false;
            }
        }
        for (uint32_t i = 0; i < access->caseCount; ++i) {
            if (!Install(&access->cases[i])) {
                return false;
            }
        }
        return true;
    }

    bool InstallStableHClass(ArkSteedHClassRef hclass) const
    {
        if (broker_ == nullptr) {
            return false;
        }
        return CheckStableHClass(hclass) && DependOnStableHClass(hclass);
    }

    bool InstallArrayDetector() const { return broker_ != nullptr && broker_->DependOnArrayDetector(); }

    bool InstallStableProtoChain(ArkSteedHClassRef receiverHClass) const
    {
        return broker_ != nullptr && broker_->DependOnStableProtoChain(receiverHClass);
    }

    bool InstallNotPrototype(ArkSteedHClassRef receiverHClass) const
    {
        if (broker_ == nullptr) {
            return false;
        }
        return DependOnNotPrototype(receiverHClass);
    }

private:
    bool Install(PropertyAccessInfo *access) const
    {
        if (access->dependencies.hclassDependency == AccessDependencyKind::HCLASS) {
            bool canAssumeStableHClass = DependOnStableHClass(access->expectedHClass);
            if (CanUseLazyDeopt() && !canAssumeStableHClass) {
                return false;
            }
            access->dependencies.canAssumeStableHClass = canAssumeStableHClass;
        }
        if (access->dependencies.protoChainDependency == AccessDependencyKind::PROTOTYPE_CHAIN) {
            bool dependOnFullProtoChain = access->IsNotFound() && !access->HasHolder();
            access->dependencies.canAssumeStableProtoChain = DependOnStableProtoChain(
                access->expectedHClass, *access, access->holderIsReceiver || dependOnFullProtoChain);
        }
        if (access->dependencies.notPrototypeDependency == AccessDependencyKind::NOT_PROTOTYPE) {
            access->dependencies.canAssumeNotPrototype = DependOnNotPrototype(access->expectedHClass);
        }
        return true;
    }

    bool Validate(const PropertyAccessInfo &access) const
    {
        if (access.dependencies.hclassDependency == AccessDependencyKind::HCLASS &&
            !CheckStableHClass(access.expectedHClass)) {
            return false;
        }
        return true;
    }

    bool CheckStableHClass(ArkSteedHClassRef hclass) const
    {
        return broker_ != nullptr && broker_->CheckStableHClass(hclass);
    }

    bool DependOnStableHClass(ArkSteedHClassRef hclass) const
    {
        return broker_ != nullptr && broker_->DependOnStableHClass(hclass);
    }

    bool DependOnStableProtoChain(ArkSteedHClassRef receiverHClass, const PropertyAccessInfo &access,
                                  bool holderIsReceiver) const
    {
        const auto &holder = holderIsReceiver ? receiverHClass : access.holderHClass;
        return broker_ != nullptr && broker_->DependOnStableProtoChain(receiverHClass, &holder);
    }

    bool DependOnNotPrototype(ArkSteedHClassRef receiverHClass) const
    {
        return broker_ != nullptr && broker_->DependOnNotPrototype(receiverHClass);
    }

    bool CanUseLazyDeopt() const
    {
        return env_ != nullptr && env_->GetJSOptions().IsEnableJitLazyDeopt();
    }

    JitCompilationEnv *env_ {nullptr};
    const ArkSteedHeapBroker *broker_ {nullptr};
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_PGO_DEPENDENCY_RECORDER_H
