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

#include "ecmascript/jit/tiering_manager.h"

#include <algorithm>
#include <cstdint>

#include "ecmascript/ecma_vm.h"
#include "ecmascript/jit/jit.h"
#include "ecmascript/js_runtime_options.h"

namespace panda::ecmascript {

namespace {
uint32_t GetHotnessThreshold(const JSThread *thread)
{
    return thread->GetEcmaVM()->GetJSOptions().GetJitHotnessThreshold();
}

uint32_t GetIcFeedbackThreshold(const JSThread *thread)
{
    return thread->GetEcmaVM()->GetJSOptions().GetJitIcFeedbackThreshold();
}
}  // namespace

int32_t TieringManager::HotnessBudgetFor(const JSThread *thread, JSHandle<Method> method, uint32_t codeSize)
{
    const int64_t len = codeSize == 0 ? static_cast<int64_t>(method->GetCodeSize(thread))
                                      : static_cast<int64_t>(codeSize);
    const TieringDecision decision = method->GetTieringDecision();
    if (len > MAX_BYTECODE_SIZE_FOR_TIERING && decision != TieringDecision::PENDING) {
        return MAX_BUDGET;
    }

    switch (decision) {
        case TieringDecision::PENDING: {
            return static_cast<int32_t>(std::min<int64_t>(len * INVOCATION_COUNT_FOR_PTI_ALLOCATION, MAX_BUDGET));
        }
        case TieringDecision::PREHEATED: {
            const uint32_t invocationCount = GetHotnessThreshold(thread);
            return static_cast<int32_t>(std::min<int64_t>(len * invocationCount, MAX_BUDGET));
        }
        case TieringDecision::ARKSTEED_REQUESTED:
            // Small re-arming window while waiting for the next call: each
            // exhaustion keeps the interpreter hitting the interrupt polling
            // path (async code installation / suspension requests).
            return static_cast<int32_t>(len);
        case TieringDecision::ARKSTEED_SUBMITTED:
        case TieringDecision::DISABLED:
            return MAX_BUDGET;
        default:
            UNREACHABLE();
    }
}

void TieringManager::NotifyICChanged(const JSThread *thread, JSTaggedValue methodValue)
{
    if (!thread->GetEcmaVM()->IsEnableFastJit() || !methodValue.IsMethod()) {
        return;
    }

    JSHandle<Method> method(thread, methodValue);
    switch (method->GetTieringDecision()) {
        case TieringDecision::PREHEATED: {
            const uint32_t threshold = GetIcFeedbackThreshold(thread);
            const int64_t len = static_cast<int64_t>(method->GetCodeSize(thread));
            int64_t newBudget = std::min<int64_t>(len * threshold, MAX_BUDGET);
            if (newBudget > method->GetHotnessCounter()) {
                method->SetHotnessCounter(static_cast<int32_t>(newBudget));
            }
            break;
        }
        default:
            break;
    }
}

void TieringManager::TryJitCompile(const JSThread *thread, JSHandle<JSFunction> func)
{
    if (!thread->GetEcmaVM()->IsEnableFastJit()) {
        return;
    }

    JSHandle<Method> method(thread, func->GetMethod(thread));
    switch (method->GetTieringDecision()) {
        case TieringDecision::PENDING: {
            method->SetTieringDecision(TieringDecision::PREHEATED);
            method->SetHotnessCounter(TieringManager::HotnessBudgetFor(thread, method));
            break;
        }
        case TieringDecision::PREHEATED: {
            method->SetTieringDecision(TieringDecision::ARKSTEED_REQUESTED);
            method->SetHotnessCounter(TieringManager::HotnessBudgetFor(thread, method));
            break;
        }
        case TieringDecision::ARKSTEED_REQUESTED:
        case TieringDecision::ARKSTEED_SUBMITTED:
        case TieringDecision::DISABLED: {
            method->SetHotnessCounter(TieringManager::HotnessBudgetFor(thread, method));
            break;
        }
        default:
            UNREACHABLE();
    }
}

void TieringManager::SubmitJitTask(const JSThread *thread, JSHandle<JSFunction> func)
{
    if (!thread->GetEcmaVM()->IsEnableFastJit()) {
        return;
    }
    JSHandle<Method> method(thread, func->GetMethod(thread));
#if ECMASCRIPT_ENABLE_ARK_STEED
    Jit::CompileArkSteed(thread->GetEcmaVM(), func, CompilerTier::Tier::ARKSTEED,
                         MachineCode::INVALID_OSR_OFFSET, JitCompileMode::Mode::ASYNC);
#endif
    method->SetTieringDecision(TieringDecision::ARKSTEED_SUBMITTED);
    method->SetHotnessCounter(TieringManager::HotnessBudgetFor(thread, method));
}

}  // namespace panda::ecmascript
