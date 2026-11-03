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

#ifndef ECMASCRIPT_ARKSTEED_COMPILATION_UNIT_H
#define ECMASCRIPT_ARKSTEED_COMPILATION_UNIT_H

#include "ecmascript/compiler/jit_compilation_env.h"
#include "ecmascript/jspandafile/method_literal.h"

namespace panda::ecmascript::arksteed {

class ArkSteedCompilationUnit {
public:
    explicit ArkSteedCompilationUnit(JitCompilationEnv *env);

    ArkSteedCompilationUnit(JitCompilationEnv *env,
                            MethodLiteral *method,
                            JSPandaFile *jsPandaFile,
                            const uint8_t *methodPcStart,
                            const JSHandle<ProfileTypeInfo> &profileTypeInfo,
                            uint32_t constantPoolMethodOffset,
                            ArkSteedCompilationUnit *caller,
                            uint32_t callerBcOffset);

    JitCompilationEnv *GetEnv() const
    {
        return env_;
    }

    MethodLiteral *GetMethod() const
    {
        return method_;
    }

    JSPandaFile *GetJSPandaFile() const
    {
        return jsPandaFile_;
    }

    const uint8_t *GetMethodPcStart() const
    {
        return methodPcStart_;
    }

    uint32_t GetBytecodeSize() const
    {
        return bytecodeSize_;
    }

    uint32_t GetMethodOffset() const
    {
        return method_->GetMethodId().GetOffset();
    }

    const JSHandle<ProfileTypeInfo> &GetProfileTypeInfo() const
    {
        return profileTypeInfo_;
    }

    uint32_t GetConstantPoolMethodOffset() const
    {
        return constantPoolMethodOffset_;
    }

    ArkSteedCompilationUnit *GetCaller() const
    {
        return caller_;
    }

    uint32_t GetCallerBcOffset() const
    {
        return callerBcOffset_;
    }

    uint32_t GetInlineDepth() const
    {
        return inlineDepth_;
    }

    bool IsTopLevel() const
    {
        return caller_ == nullptr;
    }

private:
    JitCompilationEnv *env_ {nullptr};
    MethodLiteral *method_ {nullptr};
    JSPandaFile *jsPandaFile_ {nullptr};
    const uint8_t *methodPcStart_ {nullptr};
    JSHandle<ProfileTypeInfo> profileTypeInfo_;
    uint32_t constantPoolMethodOffset_ {0};
    ArkSteedCompilationUnit *caller_ {nullptr};
    uint32_t callerBcOffset_ {0};
    uint32_t inlineDepth_ {0};
    uint32_t bytecodeSize_ {0};
};

}  // namespace panda::ecmascript::arksteed

#endif  // ECMASCRIPT_ARKSTEED_COMPILATION_UNIT_H
