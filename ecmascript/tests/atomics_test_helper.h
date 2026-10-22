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

#ifndef ECMASCRIPT_TESTS_ATOMICS_TEST_HELPER_H
#define ECMASCRIPT_TESTS_ATOMICS_TEST_HELPER_H

#include "ecmascript/base/builtins_base.h"
#include "ecmascript/base/typed_array_helper-inl.h"
#include "ecmascript/global_env.h"
#include "ecmascript/shared_objects/js_sendable_arraybuffer.h"
#include "ecmascript/shared_objects/js_shared_typed_array.h"
#include "ecmascript/tests/test_helper.h"

namespace panda::test::AtomicsTestHelper {
using namespace panda::ecmascript;

inline JSHandle<JSTypedArray> CreateArrayObject(JSThread *thread, DataViewType type, bool sendable)
{
    auto *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<JSFunction> constructor = sendable ?
        base::TypedArrayHelper::GetSharedConstructorFromType(thread, type) :
        base::TypedArrayHelper::GetConstructorFromType(thread, type);
    return JSHandle<JSTypedArray>::Cast(
        factory->NewJSObjectByConstructor(constructor, JSHandle<JSTaggedValue>::Cast(constructor)));
}

inline JSHandle<JSTaggedValue> CreateBuffer(JSThread *thread, uint32_t byteLength, bool sendable)
{
    auto *factory = thread->GetEcmaVM()->GetFactory();
    return sendable ? JSHandle<JSTaggedValue>::Cast(factory->NewJSSendableArrayBuffer(byteLength)) :
        JSHandle<JSTaggedValue>::Cast(factory->NewJSArrayBuffer(byteLength));
}

inline void InitializeArray(JSThread *thread, const JSHandle<JSTypedArray> &array,
    const JSHandle<JSTaggedValue> &buffer, DataViewType type, uint32_t length)
{
    array->SetViewedArrayBufferOrByteArray(thread, buffer.GetTaggedValue());
    array->SetContentType(type);
    array->SetByteLength(length * base::TypedArrayHelper::GetSizeFromType(type));
    array->SetByteOffset(0);
    array->SetArrayLength(length);
}

inline JSHandle<JSTaggedValue> CreateArray(JSThread *thread, DataViewType type, bool sendable)
{
    auto array = CreateArrayObject(thread, type, sendable);
    constexpr uint32_t length = 2;
    uint32_t byteLength = length * base::TypedArrayHelper::GetSizeFromType(type);
    auto buffer = CreateBuffer(thread, byteLength, sendable);
    InitializeArray(thread, array, buffer, type, length);
    return JSHandle<JSTaggedValue>::Cast(array);
}

inline JSHandle<JSTaggedValue> GetBuffer(JSThread *thread, const JSHandle<JSTaggedValue> &array)
{
    return JSHandle<JSTaggedValue>(thread,
        JSHandle<JSTypedArray>::Cast(array)->GetViewedArrayBufferOrByteArray(thread));
}

inline void DetachBuffer(JSThread *thread, const JSHandle<JSTaggedValue> &array)
{
    auto buffer = GetBuffer(thread, array);
    if (buffer->IsSendableArrayBuffer()) {
        JSHandle<JSSendableArrayBuffer>::Cast(buffer)->Detach(thread);
    } else {
        JSHandle<JSArrayBuffer>::Cast(buffer)->Detach(thread);
    }
}

inline JSHandle<JSTaggedValue> GetProperty(JSThread *thread, const JSHandle<JSTaggedValue> &object, const char *name)
{
    JSHandle<JSTaggedValue> key(thread->GetEcmaVM()->GetFactory()->NewFromASCII(name));
    return JSObject::GetProperty(thread, object, key).GetValue();
}

inline void SetProperty(JSThread *thread, const JSHandle<JSTaggedValue> &object,
    const char *name, const JSHandle<JSTaggedValue> &value)
{
    JSHandle<JSTaggedValue> key(thread->GetEcmaVM()->GetFactory()->NewFromASCII(name));
    JSObject::SetProperty(thread, object, key, value);
}

inline JSTaggedValue DetachOnValueOf(EcmaRuntimeCallInfo *argv)
{
    auto *thread = argv->GetThread();
    EcmaHandleScope scope(thread);
    JSHandle<JSTaggedValue> object = base::BuiltinsBase::GetThis(argv);
    auto array = GetProperty(thread, object, "array");
    auto result = GetProperty(thread, object, "result");
    DetachBuffer(thread, array);
    return result.GetTaggedValue();
}

inline JSHandle<JSTaggedValue> CreateValueOfObject(JSThread *thread)
{
    auto *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<GlobalEnv> env = thread->GetEcmaVM()->GetGlobalEnv();
    JSHandle<JSTaggedValue> object(factory->NewJSObjectByConstructor(
        JSHandle<JSFunction>::Cast(env->GetObjectFunction()), env->GetObjectFunction()));
    JSHandle<JSTaggedValue> valueOf(factory->NewJSFunction(env, reinterpret_cast<void *>(DetachOnValueOf)));
    JSObject::SetProperty(thread, object, thread->GlobalConstants()->GetHandledValueOfString(), valueOf);
    return object;
}

inline JSHandle<JSTaggedValue> CreateDetachingValue(JSThread *thread,
    const JSHandle<JSTaggedValue> &array, const JSHandle<JSTaggedValue> &result)
{
    auto object = CreateValueOfObject(thread);
    SetProperty(thread, object, "array", array);
    SetProperty(thread, object, "result", result);
    return object;
}
}  // namespace panda::test::AtomicsTestHelper

#endif  // ECMASCRIPT_TESTS_ATOMICS_TEST_HELPER_H
