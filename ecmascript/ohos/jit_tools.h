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
#ifndef ECMASCRIPT_JIT_TOOLS_H
#define ECMASCRIPT_JIT_TOOLS_H

#if defined(JIT_ESCAPE_ENABLE) || defined(GET_PARAMETER_FOR_JIT)
#include "base/startup/init/interfaces/innerkits/include/syspara/parameters.h"
#endif

namespace panda::ecmascript::ohos {

bool GetJitEscapeEanble()
{
#ifdef JIT_ESCAPE_ENABLE
    return OHOS::system::GetBoolParameter("ark.jit.escape.disable", false);
#endif
    return true;
}

bool IsJitEnableLitecg(bool value)
{
#ifdef GET_PARAMETER_FOR_JIT
    return OHOS::system::GetBoolParameter("ark.jit.enable.litecg", true);
#endif
    return value;
}

uint8_t GetJitCallThreshold(uint8_t threshold)
{
#ifdef GET_PARAMETER_FOR_JIT
    return OHOS::system::GetUintParameter("ark.jit.call.threshold", static_cast<uint8_t>(0));
#endif
    return threshold;
}
}
#endif  // ECMASCRIPT_JIT_TOOLS_H
