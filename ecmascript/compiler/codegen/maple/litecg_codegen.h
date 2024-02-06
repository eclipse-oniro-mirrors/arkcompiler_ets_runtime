/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_COMPILER_LITECG_CODEGEN_H
#define ECMASCRIPT_COMPILER_LITECG_CODEGEN_H

#include "ecmascript/compiler/binary_section.h"
#include "ecmascript/compiler/code_generator.h"
#include "ecmascript/compiler/litecg_ir_builder.h"

namespace panda::ecmascript::kungfu {
class CompilerLog;

class LiteCGAssembler : public Assembler {
public:
    explicit LiteCGAssembler(LMIRModule &module);
    virtual ~LiteCGAssembler() = default;
    void Run(const CompilerLog &log, bool fastCompileMode) override;
    void CollectAnStackMap(CGStackMapInfo &stackMapInfo);

private:
    LMIRModule &lmirModule;
};

class LiteCGIRGeneratorImpl : public CodeGeneratorImpl {
public:
    LiteCGIRGeneratorImpl(LMIRModule *module, bool enableLog) : module_(module), enableLog_(enableLog) {}
    ~LiteCGIRGeneratorImpl() override = default;
    void GenerateCodeForStub(Circuit *circuit, const ControlFlowGraph &graph, size_t index,
                             const CompilationConfig *cfg) override;
    void GenerateCode(Circuit *circuit, const ControlFlowGraph &graph, const CompilationConfig *cfg,
                      const MethodLiteral *methodLiteral, const JSPandaFile *jsPandaFile, const std::string &methodName,
                      bool enableOptInlining, bool enableBranchProfiling) override;

    bool IsLogEnabled() const
    {
        return enableLog_;
    }

    LMIRModule *GetModule() const
    {
        return module_;
    }

private:
    LMIRModule *module_;
    bool enableLog_ {false};
};
}  // namespace panda::ecmascript::kungfu
#endif  // ECMASCRIPT_COMPILER_LITECG_CODEGEN_H
