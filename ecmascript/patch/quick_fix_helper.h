/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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
#ifndef ECMASCRIPT_PATCH_QUICK_FIX_HELPER_H
#define ECMASCRIPT_PATCH_QUICK_FIX_HELPER_H

#include "ecmascript/js_function.h"
#include "ecmascript/napi/include/jsnapi.h"

namespace panda::ecmascript {
class QuickFixHelper {
public:
    static inline void SetPatchModule(JSThread *thread, const JSHandle<Method> &methodHandle,
                                      const JSHandle<JSFunction> &func)
    {
        if (thread->GetCurrentEcmaContext()->GetStageOfColdReload() == StageOfColdReload::COLD_RELOADING) {
            auto coldReloadRecordName =
                MethodLiteral::GetRecordName(methodHandle->GetJSPandaFile(), methodHandle->GetMethodId());
            const JSHandle<JSTaggedValue> resolvedModule =
                thread->GetCurrentEcmaContext()->FindPatchModule(coldReloadRecordName);
            if (!resolvedModule->IsHole()) {
                func->SetModule(thread, resolvedModule.GetTaggedValue());
            }
        }
    }
    static JSTaggedValue CreateMainFuncWithPatch(EcmaVM *vm, MethodLiteral *mainMethodLiteral,
                                                 const JSPandaFile *jsPandaFile);
};
}  // namespace panda::ecmascript
#endif // ECMASCRIPT_PATCH_QUICK_FIX_HELPER_H