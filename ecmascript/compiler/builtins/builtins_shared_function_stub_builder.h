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

#ifndef ECMASCRIPT_COMPILER_BUILTINS_SHARED_FUNCTION_STUB_BUILDER_H
#define ECMASCRIPT_COMPILER_BUILTINS_SHARED_FUNCTION_STUB_BUILDER_H

#include "ecmascript/compiler/builtins/builtins_function_stub_builder.h"

namespace panda::ecmascript::kungfu {
class BuiltinsSharedFunctionStubBuilder : public BuiltinsFunctionStubBuilder {
public:
    explicit BuiltinsSharedFunctionStubBuilder(StubBuilder *parent, GateRef globalEnv)
        : BuiltinsFunctionStubBuilder(parent, globalEnv) {}
    ~BuiltinsSharedFunctionStubBuilder() override = default;
    NO_MOVE_SEMANTIC(BuiltinsSharedFunctionStubBuilder);
    NO_COPY_SEMANTIC(BuiltinsSharedFunctionStubBuilder);
    void GenerateCircuit() override {}

    void PrototypeCall(GateRef glue, GateRef thisValue, GateRef numArgs, Variable* res,
                       Label *exit, Label *slowPath);
};
}  // namespace panda::ecmascript::kungfu
#endif  // ECMASCRIPT_COMPILER_BUILTINS_SHARED_FUNCTION_STUB_BUILDER_H
