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

#ifndef ECMASCRIPT_COMPILER_BUILTINS_SHARED_TYPEDARRAY_STUB_BUILDER_H
#define ECMASCRIPT_COMPILER_BUILTINS_SHARED_TYPEDARRAY_STUB_BUILDER_H

#include "ecmascript/compiler/circuit_builder.h"
#include "ecmascript/compiler/gate.h"
#include "ecmascript/compiler/stub_builder-inl.h"
#include "ecmascript/compiler/builtins/builtins_stubs.h"
#include "ecmascript/js_dataview.h"

namespace panda::ecmascript::kungfu {
class BuiltinsSharedTypedArrayStubBuilder : public BuiltinsStubBuilder {
public:
    explicit BuiltinsSharedTypedArrayStubBuilder(StubBuilder *parent, GateRef globalEnv)
        : BuiltinsStubBuilder(parent, globalEnv) {}
    ~BuiltinsSharedTypedArrayStubBuilder() override = default;
    NO_MOVE_SEMANTIC(BuiltinsSharedTypedArrayStubBuilder);
    NO_COPY_SEMANTIC(BuiltinsSharedTypedArrayStubBuilder);
    void GenerateCircuit() override {}

    void GenSharedTypedArrayConstructor(GateRef glue, GateRef nativeCode, GateRef func,
        GateRef newTarget, GateRef thisValue, GateRef numArgs, size_t ctorFuncField,
        size_t rootHClassField, size_t onHeapHClassField, const DataViewType arrayType);

private:
    // @ref TypedArrayHelper::CreateFromSendableArrayBuffer
    void CreateFromSendableArrayBuffer(Variable *result, GateRef glue, GateRef numArgs,
        GateRef buffer, Label *exit, Label *slowPath, const DataViewType arrayType);
    // @ref TypedArrayHelper::FastCopyElementFromArrayForCtor<TypedArrayKind::SHARED>
    void FastCopyElementFromArray(Variable *result, GateRef glue, GateRef array,
        GateRef rootHClass, size_t onHeapHClassField, Label *exit, Label *slowPath,
        const DataViewType arrayType);
    GateRef IsUndefOrNonNegativeSmi(GateRef value);
    // @ref TypedArrayHelper::AllocateSharedTypedArray(thread, newTarget, arrayType) —
    // the 3-arg overload.
    GateRef AllocateSharedTypedArray(GateRef glue, GateRef rootHClass, const DataViewType arrayType);
    // @ref TypedArrayHelper::AllocateSharedTypedArray(thread, newTarget, length, arrayType)
    // — the 4-arg overload.
    GateRef AllocateSharedTypedArray(GateRef glue, GateRef rootHClass, GateRef length,
        GateRef lengthTagged, size_t onHeapHClassField, const DataViewType arrayType);
    // @ref TypedArrayHelper::AllocateTypedArrayBuffer<TypedArrayKind::SHARED>.
    GateRef AllocateSharedTypedArrayBuffer(GateRef glue, GateRef typedArray, GateRef length,
        GateRef lengthTagged, GateRef rootHClass, size_t onHeapHClassField,
        const DataViewType arrayType);
};
}  // namespace panda::ecmascript::kungfu
#endif  // ECMASCRIPT_COMPILER_BUILTINS_SHARED_TYPEDARRAY_STUB_BUILDER_H
