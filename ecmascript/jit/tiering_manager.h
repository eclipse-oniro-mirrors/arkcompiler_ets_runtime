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

#ifndef ECMASCRIPT_JIT_TIERING_MANAGER_H
#define ECMASCRIPT_JIT_TIERING_MANAGER_H

#include "ecmascript/js_function.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/method.h"

namespace panda::ecmascript {

//  +----------+  hotness tick   +-----------+  hotness tick   +--------------------+
//  | PENDING  | --------------> | PREHEATED | --------------> | ARKSTEED_REQUESTED |
//  +----------+                 +-----------+                 +--------------------+
//  (warmup budget)            (standard budget)        (small re-arming window, no
//                                                              auto submission: the next
//                                                              call of the function submits;
//                                                              the ticking keeps the
//                                                              interpreter polling the
//                                                              interrupt vector)
//
//  deopt / code recycling re-arm to PREHEATED;
//  ARKSTEED_SUBMITTED / DISABLED are set outside the hotness path.

class TieringManager {
public:
    TieringManager() = delete;

    static int32_t HotnessBudgetFor(const JSThread *thread, JSHandle<Method> method, uint32_t codeSize = 0);

    static void NotifyICChanged(const JSThread *thread, JSTaggedValue method);

    static void TryJitCompile(const JSThread *thread, JSHandle<JSFunction> func);

    static void SubmitJitTask(const JSThread *thread, JSHandle<JSFunction> func);

    static constexpr int32_t MAX_BUDGET = INT32_MAX / 2;
    static constexpr int32_t INVOCATION_COUNT_FOR_PTI_ALLOCATION = 8;
    static constexpr uint32_t MAX_BYTECODE_SIZE_FOR_TIERING = 60 * 1024;    // 60KB
    static constexpr uint32_t MAX_BYTECODE_SIZE_FOR_ARKSTEED = 512 * 1024;  // 512KB
};

}  // namespace panda::ecmascript

#endif  // ECMASCRIPT_JIT_TIERING_MANAGER_H
