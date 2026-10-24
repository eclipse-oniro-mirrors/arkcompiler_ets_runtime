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

#include "ecmascript/compiler/builtins/builtins_shared_set_stub_builder.h"

#include "ecmascript/compiler/builtins/linked_hashtable_stub_builder.h"
#include "ecmascript/compiler/new_object_stub_builder.h"
#include "ecmascript/js_iterator.h"
#include "ecmascript/linked_hash_table.h"
#include "ecmascript/shared_objects/js_shared_set.h"


namespace panda::ecmascript::kungfu {

// @ref panda::ecmascript::builtins::BuiltinsSharedSet::Add()
void BuiltinsSharedSetStubBuilder::Add(GateRef glue, GateRef thisValue,
    GateRef numArgs, Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isSharedSet(env);

    // Step 1: Check this is JSSharedSet
    BRANCH(TaggedIsJSSharedSet(glue, thisValue), &isSharedSet, slowPath);
    Bind(&isSharedSet);

    // Step 2: Get value argument
    GateRef value = GetCallArg0(numArgs);

    // Step 3: check value is shared type
    Label valueIsSharedType(env);
    BRANCH(TaggedIsSharedType(glue, value), &valueIsSharedType, slowPath);
    Bind(&valueIsSharedType);

    // Step 4: JSSharedSet::Add() : LinkedHashSet::Add + SetLinkedSet inside WRITE scope
    AddImpl(glue, thisValue, value);

    // Step 5: return the set itself
    *result = thisValue;
    Jump(exit);
}

// @ref panda::ecmascript::JSSharedSet::Add()
void BuiltinsSharedSetStubBuilder::AddImpl(GateRef glue, GateRef thisValue, GateRef value)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);

    Label canWriteExit(env);
    Label writeDoneExit(env);
    Label exit(env);
    DEFVARIABLE(scopeEntered, VariableType::BOOL(), False());

    ASM_ASSERT(GET_MESSAGE_STRING_ID(SharedSetAdd), TaggedIsSharedType(glue, value));

    // @ref JSSharedSet::Add: ConcurrentApiScope<JSSharedSet, ModType::WRITE> scope(thread, set)
    ConcurrentApiScopeCanWrite<JSSharedSet>(glue, thisValue, scopeEntered, &canWriteExit);
    Bind(&canWriteExit);

    Label hasScopeException(env);
    Label notScopeException(env);
    BRANCH(HasPendingException(glue), &hasScopeException, &notScopeException);
    Bind(&hasScopeException);
    {
        Jump(&exit);
    }
    Bind(&notScopeException);
    {
        // @ref JSSharedSet::Add: JSHandle<LinkedHashSet> setHandle(... GetLinkedSet ...)
        GateRef linkedSetOffset = IntPtr(JSSharedSet::LINKED_SET_OFFSET);
        GateRef linkedTable = Load(VariableType::JS_ANY(), glue, thisValue, linkedSetOffset);

        // @ref JSSharedSet::Add: LinkedHashSet::Add(thread, setHandle, value) -> newSet
        LinkedHashTableStubBuilder<LinkedHashSet, LinkedHashSetObject> linkedHashTableStubBuilder(
            this, glue, GetCurrentGlobalEnv(), true);
        GateRef newTable = linkedHashTableStubBuilder.Insert(linkedTable, value, value);

        // @ref JSSharedSet::Add: set->SetLinkedSet(thread, newSet)
        Store(VariableType::JS_ANY(), glue, thisValue, linkedSetOffset, newTable);
        Jump(&exit);
    }
    Bind(&exit);
    ConcurrentApiScopeWriteDone<JSSharedSet>(glue, thisValue, scopeEntered, &writeDoneExit);
    Bind(&writeDoneExit);
    env->SubCfgExit();
}

// @ref panda::ecmascript::builtins::BuiltinsSharedSet::Delete()
void BuiltinsSharedSetStubBuilder::Delete(GateRef glue, GateRef thisValue,
    GateRef numArgs, Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isSharedSet(env);

    // Step 1: Check this is JSSharedSet
    // @ref Delete: if (!self->IsJSSharedSet()) { throw BIND_ERROR "The delete method cannot be bound." }
    BRANCH(TaggedIsJSSharedSet(glue, thisValue), &isSharedSet, slowPath);
    Bind(&isSharedSet);

    // Step 2: Get value argument
    // @ref Delete: JSHandle<JSTaggedValue> value = GetCallArg(argv, 0)
    GateRef value = GetCallArg0(numArgs);

    // Step 3: JSSharedSet::Delete() : FindElement + RemoveEntry inside WRITE scope
    // (DeleteImpl already converts the boolean result to a tagged boolean.)
    GateRef ret = DeleteImpl(glue, thisValue, value);

    // Step 4: return boolean
    // @ref Delete: return GetTaggedBoolean(flag)
    *result = ret;
    Jump(exit);
}

// @ref panda::ecmascript::JSSharedSet::Delete()
GateRef BuiltinsSharedSetStubBuilder::DeleteImpl(GateRef glue, GateRef thisValue, GateRef value)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    DEFVARIABLE(result, VariableType::JS_ANY(), TaggedFalse());
    Label canWriteExit(env);
    Label writeDoneExit(env);
    Label exit(env);
    DEFVARIABLE(scopeEntered, VariableType::BOOL(), False());

    // @ref JSSharedSet::Delete: ConcurrentApiScope<JSSharedSet, ModType::WRITE> scope(thread, set)
    ConcurrentApiScopeCanWrite<JSSharedSet>(glue, thisValue, scopeEntered, &canWriteExit);
    Bind(&canWriteExit);

    // RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, false): keep the default false result on
    // scope failure.
    Label hasScopeException(env);
    Label notScopeException(env);
    BRANCH(HasPendingException(glue), &hasScopeException, &notScopeException);
    Bind(&hasScopeException);
    {
        Jump(&exit);
    }
    Bind(&notScopeException);
    {
        GateRef linkedSetOffset = IntPtr(JSSharedSet::LINKED_SET_OFFSET);
        GateRef linkedTable = Load(VariableType::JS_ANY(), glue, thisValue, linkedSetOffset);
        LinkedHashTableStubBuilder<LinkedHashSet, LinkedHashSetObject> linkedHashTableStubBuilder(
            this, glue, GetCurrentGlobalEnv(), true);
        result = BooleanToTaggedBooleanPtr(linkedHashTableStubBuilder.Delete(linkedTable, value));
        Jump(&exit);
    }
    Bind(&exit);
    ConcurrentApiScopeWriteDone<JSSharedSet>(glue, thisValue, scopeEntered, &writeDoneExit);
    Bind(&writeDoneExit);
    auto res = *result;
    env->SubCfgExit();
    return res;
}

// @ref panda::ecmascript::builtins::BuiltinsSharedSet::Has()
void BuiltinsSharedSetStubBuilder::Has(GateRef glue, GateRef thisValue,
    GateRef numArgs, Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isSharedSet(env);

    // Step 1: Check this is JSSharedSet
    // @ref Has: if (!self->IsJSSharedSet()) { throw BIND_ERROR "The has method cannot be bound." }
    BRANCH(TaggedIsJSSharedSet(glue, thisValue), &isSharedSet, slowPath);
    Bind(&isSharedSet);

    // Step 2: Get value argument
    // @ref Has: JSHandle<JSTaggedValue> value = GetCallArg(argv, 0)
    GateRef value = GetCallArg0(numArgs);

    // Step 3: JSSharedSet::Has() : LinkedHashSet::Has inside READ scope
    // (HasImpl already converts the boolean result to a tagged boolean.)
    GateRef ret = HasImpl(glue, thisValue, value);

    // Step 4: return boolean
    // @ref Has: return GetTaggedBoolean(flag)
    *result = ret;
    Jump(exit);
}

// @ref panda::ecmascript::JSSharedSet::Has()
GateRef BuiltinsSharedSetStubBuilder::HasImpl(GateRef glue, GateRef thisValue, GateRef value)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);
    DEFVARIABLE(result, VariableType::JS_ANY(), TaggedFalse());
    Label canReadExit(env);
    Label readDoneExit(env);
    Label exit(env);
    DEFVARIABLE(expectModRecord, VariableType::INT32(), Int32(0));
    DEFVARIABLE(desiredModRecord, VariableType::INT32(), Int32(0));
    DEFVARIABLE(scopeEntered, VariableType::BOOL(), False());

    // @ref JSSharedSet::Has: ConcurrentApiScope<JSSharedSet> scope(thread, set)
    ConcurrentApiScopeCanRead<JSSharedSet>(glue, thisValue, expectModRecord,
        desiredModRecord, scopeEntered, &canReadExit);
    Bind(&canReadExit);

    // RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, false): keep the default false result on
    // scope failure.
    Label hasScopeException(env);
    Label notScopeException(env);
    BRANCH(HasPendingException(glue), &hasScopeException, &notScopeException);
    Bind(&hasScopeException);
    {
        Jump(&exit);
    }
    Bind(&notScopeException);
    {
        // @ref JSSharedSet::Has: LinkedHashSet::Cast(...GetLinkedSet...)->Has(thread, value)
        GateRef linkedSetOffset = IntPtr(JSSharedSet::LINKED_SET_OFFSET);
        GateRef linkedTable = Load(VariableType::JS_ANY(), glue, thisValue, linkedSetOffset);
        LinkedHashTableStubBuilder<LinkedHashSet, LinkedHashSetObject> linkedHashTableStubBuilder(
            this, glue, GetCurrentGlobalEnv(), true);
        result = BooleanToTaggedBooleanPtr(linkedHashTableStubBuilder.Has(linkedTable, value));
        Jump(&exit);
    }
    Bind(&exit);
    ConcurrentApiScopeReadDone<JSSharedSet>(glue, thisValue, expectModRecord,
        desiredModRecord, scopeEntered, &readDoneExit);
    Bind(&readDoneExit);
    auto res = *result;
    env->SubCfgExit();
    return res;
}

// @ref panda::ecmascript::builtins::BuiltinsSharedSet::Clear()
void BuiltinsSharedSetStubBuilder::Clear(GateRef glue, GateRef thisValue,
    GateRef numArgs, Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isSharedSet(env);

    // Step 1: Check this is JSSharedSet
    // @ref Clear: if (!self->IsJSSharedSet()) { throw BIND_ERROR "The clear method cannot be bound." }
    BRANCH(TaggedIsJSSharedSet(glue, thisValue), &isSharedSet, slowPath);
    Bind(&isSharedSet);

    // Step 2: JSSharedSet::Clear()
    ClearImpl(glue, thisValue);

    // Step 3: return undefined
    *result = Undefined();
    Jump(exit);
}

// @ref panda::ecmascript::JSSharedSet::Clear()
void BuiltinsSharedSetStubBuilder::ClearImpl(GateRef glue, GateRef thisValue)
{
    auto env = GetEnvironment();
    Label entry(env);
    env->SubCfgEntry(&entry);

    Label canWriteExit(env);
    Label writeDoneExit(env);
    Label exit(env);
    DEFVARIABLE(scopeEntered, VariableType::BOOL(), False());

    // @ref ConcurrentApiScope<JSSharedSet, ModType::WRITE> scope(thread, set)
    ConcurrentApiScopeCanWrite<JSSharedSet>(glue, thisValue, scopeEntered, &canWriteExit);
    Bind(&canWriteExit);

    // RETURN_IF_ABRUPT_COMPLETION(thread) right after the scope acquire.
    Label hasScopeException(env);
    Label notScopeException(env);
    BRANCH(HasPendingException(glue), &hasScopeException, &notScopeException);
    Bind(&hasScopeException);
    {
        Jump(&exit);
    }
    Bind(&notScopeException);
    {
        // @ref JSHandle<LinkedHashSet> setHandle(... GetLinkedSet ...)
        GateRef linkedSetOffset = IntPtr(JSSharedSet::LINKED_SET_OFFSET);
        GateRef linkedTable = Load(VariableType::JS_ANY(), glue, thisValue, linkedSetOffset);

        // @ref JSHandle<LinkedHashSet> newSet = LinkedHashSet::Clear(thread, setHandle);
        LinkedHashTableStubBuilder<LinkedHashSet, LinkedHashSetObject> linkedHashTableStubBuilder(
            this, glue, GetCurrentGlobalEnv(), true);
        GateRef newTable = linkedHashTableStubBuilder.Clear(linkedTable);

        // @ref set->SetLinkedSet(thread, newSet)
        Store(VariableType::JS_ANY(), glue, thisValue, linkedSetOffset, newTable);
        Jump(&exit);
    }
    Bind(&exit);
    ConcurrentApiScopeWriteDone<JSSharedSet>(glue, thisValue, scopeEntered, &writeDoneExit);
    Bind(&writeDoneExit);
    env->SubCfgExit();
}

// @ref panda::ecmascript::builtins::BuiltinsSharedSet::Values()
void BuiltinsSharedSetStubBuilder::Values(GateRef glue, GateRef thisValue,
    GateRef numArgs, Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isSharedSet(env);

    // Step 1: Check this is JSSharedSet
    // @ref Values: if (!self->IsJSSharedSet()) { throw BIND_ERROR "The values method cannot be bound." }
    BRANCH(TaggedIsJSSharedSet(glue, thisValue), &isSharedSet, slowPath);
    Bind(&isSharedSet);

    // Step 2: create a JSSharedSetIterator with IterationKind::VALUE
    // @ref Values: JSHandle<JSTaggedValue> iter =
    //     JSSharedSetIterator::CreateSetIterator(thread, self, IterationKind::VALUE)
    NewObjectStubBuilder newBuilder(this);
    newBuilder.SetParameters(glue, IntPtr(JSSharedSetIterator::SIZE));
    GateRef kind = Int32(static_cast<int32_t>(IterationKind::VALUE));
    newBuilder.CreateJSSharedSetIterator(result, exit, thisValue, kind);
}

// @ref panda::ecmascript::builtins::BuiltinsSharedSet::Entries()
void BuiltinsSharedSetStubBuilder::Entries(GateRef glue, GateRef thisValue,
    GateRef numArgs, Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isSharedSet(env);

    // Step 1: Check this is JSSharedSet
    // @ref Entries: if (!self->IsJSSharedSet()) { throw BIND_ERROR "The entries method cannot be bound." }
    BRANCH(TaggedIsJSSharedSet(glue, thisValue), &isSharedSet, slowPath);
    Bind(&isSharedSet);

    // Step 2: create a JSSharedSetIterator with IterationKind::KEY_AND_VALUE
    // @ref Entries: JSHandle<JSTaggedValue> iter =
    //     JSSharedSetIterator::CreateSetIterator(thread, self, IterationKind::KEY_AND_VALUE)
    NewObjectStubBuilder newBuilder(this);
    newBuilder.SetParameters(glue, IntPtr(JSSharedSetIterator::SIZE));
    GateRef kind = Int32(static_cast<int32_t>(IterationKind::KEY_AND_VALUE));
    newBuilder.CreateJSSharedSetIterator(result, exit, thisValue, kind);
}

// @ref panda::ecmascript::builtins::BuiltinsSharedSet::ForEach()
void BuiltinsSharedSetStubBuilder::ForEach(GateRef glue, GateRef thisValue,
    GateRef numArgs, Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isSharedSet(env);

    // Step 1: Check this is JSSharedSet
    // @ref ForEach: if (!self->IsJSSharedSet()) { throw BIND_ERROR "The forEach method cannot be bound." }
    BRANCH(TaggedIsJSSharedSet(glue, thisValue), &isSharedSet, slowPath);
    Bind(&isSharedSet);

    // Step 2: acquire the READ scope
    Label canReadExit(env);
    Label readDoneExit(env);
    Label scopeRelease(env);
    Label hasScopeException(env);
    Label notScopeException(env);
    Label callbackIsCallable(env);
    Label notCallable(env);
    DEFVARIABLE(res, VariableType::JS_ANY(), Undefined());
    DEFVARIABLE(expectModRecord, VariableType::INT32(), Int32(0));
    DEFVARIABLE(desiredModRecord, VariableType::INT32(), Int32(0));
    DEFVARIABLE(scopeEntered, VariableType::BOOL(), False());

    // @ref ForEach: ConcurrentApiScope<JSSharedSet> scope(thread, self)
    ConcurrentApiScopeCanRead<JSSharedSet>(glue, thisValue, expectModRecord,
        desiredModRecord, scopeEntered, &canReadExit);
    Bind(&canReadExit);

    // @ref ForEach: RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, JSTaggedValue::Exception())
    BRANCH(HasPendingException(glue), &hasScopeException, &notScopeException);
    Bind(&hasScopeException);
    {
        res = Exception();
        Jump(&scopeRelease);
    }
    Bind(&notScopeException);
    {
        // Step 3: callable check — INSIDE the scope in C++; failure throws
        // in-place and the scope release goes through scopeRelease (RAII parity).
        // @ref ForEach: if (!func->IsCallable()) { throw TYPE_ERROR "callback is not callable" }
        GateRef callbackFnHandle = GetCallArg0(numArgs);
        BRANCH_LIKELY(TaggedIsCallable(glue, callbackFnHandle), &callbackIsCallable, &notCallable);
        Bind(&callbackIsCallable);
        {
            // Step 4: iterate the LinkedHashSet.
            // @ref ForEach: iterate entries in original insertion order, skipping deleted
            // (hole) entries, calling func(key, key, set) for each.
            GateRef thisArg = GetCallArg1(numArgs);
            GateRef linkedSetOffset = IntPtr(JSSharedSet::LINKED_SET_OFFSET);
            GateRef linkedTable = Load(VariableType::JS_ANY(), glue, thisValue, linkedSetOffset);
            LinkedHashTableStubBuilder<LinkedHashSet, LinkedHashSetObject> linkedHashTableStubBuilder(
                this, glue, GetCurrentGlobalEnv(), true);
            res = linkedHashTableStubBuilder.SendableForEach(thisValue, linkedTable, callbackFnHandle, thisArg);
            Jump(&scopeRelease);
        }
        Bind(&notCallable);
        {
            // @ref: THROW_TYPE_ERROR_AND_RETURN "callback is not callable"
            IR_IF (HasPendingException(glue)) {
                res = Exception();
            } IR_ELSE {
                CallRuntime(glue, RTSTUB_ID(ThrowTypeError),
                    { IntToTaggedInt(Int32(GET_MESSAGE_STRING_ID(CallbackNotCallable))) });
                res = Exception();
            }
            Jump(&scopeRelease);
        }
    }
    Bind(&scopeRelease);
    // Release the read scope on every path (normal / callback exception / callable failure /
    // scope failure), matching the C++ ConcurrentApiScope RAII destructor.
    ConcurrentApiScopeReadDone<JSSharedSet>(glue, thisValue, expectModRecord,
        desiredModRecord, scopeEntered, &readDoneExit);
    Bind(&readDoneExit);

    // Step 5: propagate result
    // @ref ForEach: return JSTaggedValue::Undefined() on success;
    //               RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, ret) on callback exception.
    *result = *res;
    Jump(exit);
}

// @ref panda::ecmascript::builtins::BuiltinsSharedSet::GetSize()
void BuiltinsSharedSetStubBuilder::GetSize(GateRef glue, GateRef thisValue,
    GateRef numArgs, Variable *result, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label isSharedSet(env);

    // Step 1: Check this is JSSharedSet
    // @ref GetSize: if (!self->IsJSSharedSet()) { throw TYPE_ERROR "obj is not SharedSet" }
    BRANCH(TaggedIsJSSharedSet(glue, thisValue), &isSharedSet, slowPath);
    Bind(&isSharedSet);

    // Step 2: JSSharedSet::GetSize() : LinkedHashSet::NumberOfElements inside READ scope
    Label canReadExit(env);
    Label readDoneExit(env);
    Label scopeRelease(env);
    DEFVARIABLE(res, VariableType::JS_ANY(), IntToTaggedPtr(Int32(0)));
    DEFVARIABLE(expectModRecord, VariableType::INT32(), Int32(0));
    DEFVARIABLE(desiredModRecord, VariableType::INT32(), Int32(0));
    DEFVARIABLE(scopeEntered, VariableType::BOOL(), False());

    // @ref JSSharedSet::GetSize: ConcurrentApiScope<JSSharedSet> scope(thread, set)
    ConcurrentApiScopeCanRead<JSSharedSet>(glue, thisValue, expectModRecord,
        desiredModRecord, scopeEntered, &canReadExit);
    Bind(&canReadExit);

    // RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, 0): on scope failure return 0.
    Label hasScopeException(env);
    Label notScopeException(env);
    BRANCH(HasPendingException(glue), &hasScopeException, &notScopeException);
    Bind(&hasScopeException);
    {
        res = IntToTaggedPtr(Int32(0));
        Jump(&scopeRelease);
    }
    Bind(&notScopeException);
    {
        // Read the NumberOfElements slot of the linked table. The hashtable builder's
        // GetNumberOfElements is private, so read the count via the public tagged-array
        // primitive, exactly as the private helper does.
        GateRef linkedSetOffset = IntPtr(JSSharedSet::LINKED_SET_OFFSET);
        GateRef linkedTable = Load(VariableType::JS_ANY(), glue, thisValue, linkedSetOffset);
        GateRef elementsIndex = Int32(LinkedHashSet::NUMBER_OF_ELEMENTS_INDEX);
        GateRef numElements = TaggedGetInt(GetValueFromTaggedArray(glue, linkedTable, elementsIndex));
        res = IntToTaggedPtr(numElements);
        Jump(&scopeRelease);
    }
    Bind(&scopeRelease);
    ConcurrentApiScopeReadDone<JSSharedSet>(glue, thisValue, expectModRecord,
        desiredModRecord, scopeEntered, &readDoneExit);
    Bind(&readDoneExit);

    // Step 3: return the size as a tagged int
    // @ref GetSize: return JSTaggedValue(size)
    // The C++ entry's extra exception check after JSSharedSet::GetSize is redundant
    // (the slot read cannot throw); the IR omits it with identical behavior.
    *result = *res;
    Jump(exit);
}

}  // namespace panda::ecmascript::kungfu
