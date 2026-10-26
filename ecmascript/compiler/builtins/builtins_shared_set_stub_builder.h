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

#ifndef ECMASCRIPT_COMPILER_BUILTINS_SHARED_SET_STUB_BUILDER_H
#define ECMASCRIPT_COMPILER_BUILTINS_SHARED_SET_STUB_BUILDER_H

#include "ecmascript/compiler/circuit_builder.h"
#include "ecmascript/compiler/gate.h"
#include "ecmascript/compiler/stub_builder-inl.h"
#include "ecmascript/compiler/builtins/builtins_stubs.h"
#include "ecmascript/shared_objects/js_shared_set_iterator.h"

namespace panda::ecmascript::kungfu {
class BuiltinsSharedSetStubBuilder : public BuiltinsStubBuilder {
public:
    explicit BuiltinsSharedSetStubBuilder(StubBuilder *parent, GateRef globalEnv)
        : BuiltinsStubBuilder(parent, globalEnv) {}
    ~BuiltinsSharedSetStubBuilder() override = default;
    NO_MOVE_SEMANTIC(BuiltinsSharedSetStubBuilder);
    NO_COPY_SEMANTIC(BuiltinsSharedSetStubBuilder);
    void GenerateCircuit() override {}

    void Add(GateRef glue, GateRef thisValue,
        GateRef numArgs, Variable *result, Label *exit, Label *slowPath);
    void Delete(GateRef glue, GateRef thisValue,
        GateRef numArgs, Variable *result, Label *exit, Label *slowPath);
    void Has(GateRef glue, GateRef thisValue,
        GateRef numArgs, Variable *result, Label *exit, Label *slowPath);
    void Clear(GateRef glue, GateRef thisValue,
        GateRef numArgs, Variable *result, Label *exit, Label *slowPath);
    void Values(GateRef glue, GateRef thisValue,
        GateRef numArgs, Variable *result, Label *exit, Label *slowPath);
    void Entries(GateRef glue, GateRef thisValue,
        GateRef numArgs, Variable *result, Label *exit, Label *slowPath);
    void ForEach(GateRef glue, GateRef thisValue,
        GateRef numArgs, Variable *result, Label *exit, Label *slowPath);
    void GetSize(GateRef glue, GateRef thisValue,
        GateRef numArgs, Variable *result, Label *exit, Label *slowPath);

private:
    void AddImpl(GateRef glue, GateRef thisValue, GateRef value);
    GateRef DeleteImpl(GateRef glue, GateRef thisValue, GateRef value);
    GateRef HasImpl(GateRef glue, GateRef thisValue, GateRef value);
    void ClearImpl(GateRef glue, GateRef thisValue);
};
}  // namespace panda::ecmascript::kungfu
#endif  // ECMASCRIPT_COMPILER_BUILTINS_SHARED_SET_STUB_BUILDER_H
