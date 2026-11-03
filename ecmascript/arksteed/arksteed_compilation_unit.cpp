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

#include "ecmascript/arksteed/arksteed_compilation_unit.h"

namespace panda::ecmascript::arksteed {

ArkSteedCompilationUnit::ArkSteedCompilationUnit(JitCompilationEnv *env)
    : ArkSteedCompilationUnit(env,
                             env->GetMethodLiteral(),
                             env->GetJSPandaFile(),
                             env->GetMethodPcStart(),
                             env->GetProfileTypeInfo(),
                             env->GetMethodLiteral()->GetMethodId().GetOffset(),
                             nullptr,
                             0)
{}

ArkSteedCompilationUnit::ArkSteedCompilationUnit(JitCompilationEnv *env,
                                                 MethodLiteral *method,
                                                 JSPandaFile *jsPandaFile,
                                                 const uint8_t *methodPcStart,
                                                 const JSHandle<ProfileTypeInfo> &profileTypeInfo,
                                                 uint32_t constantPoolMethodOffset,
                                                 ArkSteedCompilationUnit *caller,
                                                 uint32_t callerBcOffset)
    : env_(env),
      method_(method),
      jsPandaFile_(jsPandaFile),
      methodPcStart_(methodPcStart),
      profileTypeInfo_(profileTypeInfo),
      constantPoolMethodOffset_(constantPoolMethodOffset),
      caller_(caller),
      callerBcOffset_(callerBcOffset),
      inlineDepth_(caller == nullptr ? 0 : caller->GetInlineDepth() + 1),
      bytecodeSize_(MethodLiteral::GetCodeSize(jsPandaFile, method->GetMethodId()))
{
    ASSERT(env_ != nullptr);
    ASSERT(method_ != nullptr);
    ASSERT(jsPandaFile_ != nullptr);
    ASSERT(methodPcStart_ != nullptr);
}

}  // namespace panda::ecmascript::arksteed
