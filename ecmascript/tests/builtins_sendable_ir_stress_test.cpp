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

/*
 * multi-worker concurrent invocation of sendable
 * builtins (hasOwnProperty / toString / Object.keys) on a shared object, with
 * allocation pressure on the shared heap.
 */

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "ecmascript/builtins/builtins_object.h"
#include "ecmascript/interpreter/interpreter.h"
#include "ecmascript/js_array.h"
#include "ecmascript/js_function.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_object.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/napi/include/jsnapi.h"
#include "ecmascript/object_factory-inl.h"
#include "ecmascript/tests/ecma_test_common.h"

using namespace panda;
using namespace panda::ecmascript;

namespace panda::test {
namespace {
constexpr int STRESS_ROUNDS = 200;
constexpr size_t GARBAGE_LEN = 256;
constexpr size_t GARBAGE_PER_ROUND = 4;
constexpr int WORKER_COUNT = 2;
constexpr int32_t NON_STRING_TAG_VALUE = 42;
constexpr int EXPECTED_TAG_GETTER_CALLS = 2;
constexpr uint32_t SHARED_OBJ_FIELD_COUNT = 2;

// Counts @@toStringTag getter invocations for the toString compat test (single-threaded body).
std::atomic<int> g_toStringTagGetterCalls {0};

JSValueRef ToStringTagGetter(JsiRuntimeCallInfo *info)
{
    int n = g_toStringTagGetterCalls.fetch_add(1) + 1;
    std::string tag = "Tag" + std::to_string(n);
    return **StringRef::NewFromUtf8(info->GetVM(), tag.c_str());
}

// Records the value each recording setter receives for the assign order test (single-threaded
// body). Source property values are unique, so the value sequence is the assignment order.
std::vector<int32_t> g_assignSetterValues;

Local<JSValueRef> AssignOrderSetter(JsiRuntimeCallInfo *info)
{
    Local<JSValueRef> value = info->GetCallArgRef(0);
    g_assignSetterValues.push_back(value->Int32Value(info->GetVM()));
    return JSValueRef::Undefined(info->GetVM());
}

// Counts getter invocations for the non-string accessor tag shape-guard test (single-threaded).
std::atomic<int> g_nonStringTagGetterCalls {0};

JSValueRef NonStringTagGetter(JsiRuntimeCallInfo *info)
{
    g_nonStringTagGetterCalls.fetch_add(1);
    return **Local<JSValueRef>(IntegerRef::New(info->GetVM(), NON_STRING_TAG_VALUE));
}

// Getter for the flip-tag test: first call returns a non-string, second call a String.
std::atomic<int> g_flipTagGetterCalls {0};

JSValueRef FlipTagGetter(JsiRuntimeCallInfo *info)
{
    int n = g_flipTagGetterCalls.fetch_add(1) + 1;
    if (n == 1) {
        return **Local<JSValueRef>(IntegerRef::New(info->GetVM(), NON_STRING_TAG_VALUE));
    }
    return **Local<JSValueRef>(StringRef::NewFromUtf8(info->GetVM(), "Flip2"));
}

struct StressCtx {
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<uint64_t> sharedObjRaw {0};
    std::atomic<bool> objReady {false};
    std::atomic<int> failedWorkers {0};
};

JSTaggedValue InvokeMethod(JSThread *thread, const JSHandle<JSTaggedValue> &func,
                           const JSHandle<JSTaggedValue> &thisArg, const std::vector<JSTaggedValue> &args)
{
    EcmaRuntimeCallInfo *info = EcmaInterpreter::NewRuntimeCallInfo(thread, func, thisArg,
        JSHandle<JSTaggedValue>(thread, JSTaggedValue::Undefined()), args.size());
    for (uint32_t i = 0; i < args.size(); i++) {
        info->SetCallArg(i, args[i]);
    }
    return JSFunction::Call(info);
}

bool StressOneRound(JSThread *thread, EcmaVM *vm, const JSHandle<JSTaggedValue> &sharedObj)
{
    JSHandle<GlobalEnv> env = vm->GetGlobalEnv();
    ObjectFactory *factory = vm->GetFactory();
    JSHandle<JSTaggedValue> sObjPrototype = env->GetSObjectFunctionPrototype();
    JSHandle<JSTaggedValue> keyX(factory->NewFromASCII("x"));
    JSHandle<JSTaggedValue> keyMissing(factory->NewFromASCII("missing"));

    // 1. SharedObject.prototype.hasOwnProperty with shared this
    JSHandle<JSTaggedValue> hasOwn = JSObject::GetMethod(
        thread, sObjPrototype, JSHandle<JSTaggedValue>(factory->NewFromASCII("hasOwnProperty")));
    if (hasOwn.IsEmpty() || hasOwn->IsUndefined()) {
        return false;
    }
    JSTaggedValue r1 = InvokeMethod(thread, hasOwn, sharedObj, {keyX.GetTaggedValue()});
    if (!r1.IsTrue() || thread->HasPendingException()) {
        return false;
    }
    JSTaggedValue r2 = InvokeMethod(thread, hasOwn, sharedObj, {keyMissing.GetTaggedValue()});
    if (!r2.IsFalse() || thread->HasPendingException()) {
        return false;
    }

    // 2. SharedObject.prototype.toString with shared this
    JSHandle<JSTaggedValue> toStringFn = JSObject::GetMethod(
        thread, sObjPrototype, JSHandle<JSTaggedValue>(factory->NewFromASCII("toString")));
    JSTaggedValue tag = InvokeMethod(thread, toStringFn, sharedObj, {});
    if (!tag.IsString() || thread->HasPendingException()) {
        return false;
    }

    // 3. worker-VM Object.keys with shared argument (same IR stub path)
    JSHandle<JSTaggedValue> objectFn = env->GetObjectFunction();
    JSHandle<JSTaggedValue> keysFn = JSObject::GetMethod(
        thread, objectFn, JSHandle<JSTaggedValue>(factory->NewFromASCII("keys")));
    JSTaggedValue keys = InvokeMethod(thread, keysFn,
        JSHandle<JSTaggedValue>(thread, JSTaggedValue::Undefined()), {sharedObj.GetTaggedValue()});
    if (!keys.IsJSArray() || JSArray::Cast(keys)->GetArrayLength() != SHARED_OBJ_FIELD_COUNT ||
        thread->HasPendingException()) {
        return false;
    }
    return true;
}

void WorkerStressRun(StressCtx *ctx)
{
    JSRuntimeOptions options;
    options.SetEnableForceGC(false);
    EcmaVM *workerVm = JSNApi::CreateEcmaVM(options);
    if (workerVm == nullptr) {
        ctx->failedWorkers.fetch_add(1);
        return;
    }
    JSThread *workerThread = workerVm->GetJSThread();
    workerThread->ManagedCodeBegin();
    bool localOk = true;
    {
        {
            std::unique_lock<std::mutex> lock(ctx->mtx);
            ctx->cv.wait(lock, [ctx] { return ctx->objReady.load(); });
        }
        JSTaggedValue sharedVal = JSTaggedValue(ctx->sharedObjRaw.load());
        if (!sharedVal.IsHeapObject() || !sharedVal.IsInSharedHeap()) {
            localOk = false;
        } else {
            ObjectFactory *factory = workerVm->GetFactory();
            EcmaHandleScope scope(workerThread);
            JSHandle<JSTaggedValue> sharedHandle(workerThread, sharedVal);
            for (int round = 0; round < STRESS_ROUNDS; round++) {
                {
                    EcmaHandleScope roundScope(workerThread);
                    if (!StressOneRound(workerThread, workerVm, sharedHandle)) {
                        localOk = false;
                        break;
                    }
                }
                // allocation pressure on the shared heap
                {
                    EcmaHandleScope garbageScope(workerThread);
                    for (size_t i = 0; i < GARBAGE_PER_ROUND; i++) {
                        factory->NewSTaggedArray(GARBAGE_LEN, JSTaggedValue::Undefined());
                    }
                }
            }
        }
    }
    workerThread->ManagedCodeEnd();
    JSNApi::DestroyJSVM(workerVm);
    if (!localOk) {
        ctx->failedWorkers.fetch_add(1);
    }
}
}  // namespace

class SendableBuiltinsIRStressTest : public BaseTestWithScope<false> {};

/*
 * Main thread and 2 workers concurrently invoke sendable builtins on the same
 * shared object while allocating shared garbage. No crash / no wrong result.
 */
HWTEST_F_L0(SendableBuiltinsIRStressTest, ConcurrentSendableBuiltinsStress)
{
    ObjectFactory *factory = instance->GetFactory();
    JSHandle<GlobalEnv> env = instance->GetGlobalEnv();
    // Shared object with two enumerable own fields x=1, y=2 (proto roots at
    // SharedObject.prototype; descriptor usage follows JSNapiSendable::InitWithPropertiesInfo).
    std::vector<PropertyDescriptor> descs;
    JSHandle<JSTaggedValue> xKey(factory->NewFromASCII("x"));
    JSHandle<JSTaggedValue> yKey(factory->NewFromASCII("y"));
    PropertyDescriptor xDesc(thread, true, true, true);
    xDesc.SetKey(xKey);
    xDesc.SetSharedFieldType(SharedFieldType::NUMBER);
    xDesc.SetValue(JSHandle<JSTaggedValue>(thread, JSTaggedValue(1)));
    PropertyDescriptor yDesc(thread, true, true, true);
    yDesc.SetKey(yKey);
    yDesc.SetSharedFieldType(SharedFieldType::NUMBER);
    yDesc.SetValue(JSHandle<JSTaggedValue>(thread, JSTaggedValue(2)));
    descs.push_back(xDesc);
    descs.push_back(yDesc);
    JSHandle<JSTaggedValue> sharedObj = factory->CreateSObjectWithProperties(descs);
    ASSERT_TRUE(sharedObj->IsHeapObject());
    ASSERT_TRUE(sharedObj->IsInSharedHeap());

    StressCtx ctx;
    ctx.sharedObjRaw.store(sharedObj.GetTaggedValue().GetRawData());
    {
        std::lock_guard<std::mutex> lock(ctx.mtx);
        ctx.objReady.store(true);
        ctx.cv.notify_all();
    }

    std::vector<std::thread> workers;
    for (int i = 0; i < WORKER_COUNT; i++) {
        workers.emplace_back(WorkerStressRun, &ctx);
    }

    // Main-thread concurrent loop (thread state is managed by the fixture).
    bool mainOk = true;
    {
        EcmaHandleScope scope(thread);
        JSHandle<JSTaggedValue> sObjPrototype = env->GetSObjectFunctionPrototype();
        JSHandle<JSTaggedValue> hasOwn = JSObject::GetMethod(
            thread, sObjPrototype, JSHandle<JSTaggedValue>(factory->NewFromASCII("hasOwnProperty")));
        JSTaggedValue xKey = JSHandle<JSTaggedValue>(factory->NewFromASCII("x")).GetTaggedValue();
        JSTaggedValue missingKey = JSHandle<JSTaggedValue>(factory->NewFromASCII("missing")).GetTaggedValue();
        for (int round = 0; round < STRESS_ROUNDS; round++) {
            EcmaHandleScope roundScope(thread);
            JSTaggedValue r1 = InvokeMethod(thread, hasOwn, sharedObj, {xKey});
            JSTaggedValue r2 = InvokeMethod(thread, hasOwn, sharedObj, {missingKey});
            if (!r1.IsTrue() || !r2.IsFalse() || thread->HasPendingException()) {
                mainOk = false;
                break;
            }
        }
    }

    for (auto &w : workers) {
        w.join();
    }
    ASSERT_TRUE(mainOk);
    ASSERT_EQ(ctx.failedWorkers.load(), 0);
}

 /*
  * a JS_SHARED_OBJECT source with layout [Symbol.iterator, "name"] (NAPI-only
  * constructible) must keep the pre-change C++ order "strings before symbols".
  * Symbol.iterator is a shared-heap well-known symbol.
  */
HWTEST_F_L0(SendableBuiltinsIRStressTest, AssignSharedSourceWithSymbolLayoutKeyKeepsCppOrder)
{
    ObjectFactory *factory = instance->GetFactory();
    JSHandle<GlobalEnv> env = instance->GetGlobalEnv();

    // Shared source with layout [Symbol.iterator, "name"], both enumerable data fields.
    JSHandle<JSTaggedValue> symKey = thread->GlobalConstants()->GetHandledIteratorSymbol();
    JSHandle<JSTaggedValue> nameKey(factory->NewFromASCII("name"));
    std::vector<PropertyDescriptor> descs;
    PropertyDescriptor symDesc(thread, true, true, true);
    symDesc.SetKey(symKey);
    symDesc.SetSharedFieldType(SharedFieldType::NUMBER);
    symDesc.SetValue(JSHandle<JSTaggedValue>(thread, JSTaggedValue(1)));
    descs.push_back(symDesc);
    PropertyDescriptor nameDesc(thread, true, true, true);
    nameDesc.SetKey(nameKey);
    nameDesc.SetSharedFieldType(SharedFieldType::NUMBER);
    nameDesc.SetValue(JSHandle<JSTaggedValue>(thread, JSTaggedValue(2)));
    descs.push_back(nameDesc);
    JSHandle<JSTaggedValue> sharedSrc = factory->CreateSObjectWithProperties(descs);
    ASSERT_TRUE(sharedSrc->IsJSSharedObject());

    // Plain target with a non-writable "name": the strict [[Set]] of Object.assign throws.
    JSHandle<JSObject> target = factory->OrdinaryNewJSObjectCreate(env->GetObjectFunctionPrototype());
    PropertyDescriptor nonWritableDesc(thread, false, true, false);
    nonWritableDesc.SetValue(JSHandle<JSTaggedValue>(thread, JSTaggedValue(0)));
    ASSERT_TRUE(JSTaggedValue::DefineOwnProperty(thread, JSHandle<JSTaggedValue>(target), nameKey,
                                                 nonWritableDesc));

    // Object.assign(target, sharedSrc) through the interpreter (IR stub dispatch path).
    JSHandle<JSTaggedValue> objectFn = env->GetObjectFunction();
    JSHandle<JSTaggedValue> assignFn = JSObject::GetMethod(
        thread, objectFn, JSHandle<JSTaggedValue>(factory->NewFromASCII("assign")));
    ASSERT_TRUE(!assignFn.IsEmpty() && !assignFn->IsUndefined());
    {
        EcmaHandleScope scope(thread);
        InvokeMethod(thread, assignFn, JSHandle<JSTaggedValue>(thread, JSTaggedValue::Undefined()),
                     {target.GetTaggedValue(), sharedSrc.GetTaggedValue()});
    }
    // Strings are assigned before symbols in the C++ order, so "name" throws first and the
    // symbol key must not have been copied yet.
    EXPECT_EXCEPTION();
    JSHandle<JSTaggedValue> targetVal(thread, target.GetTaggedValue());
    ASSERT_FALSE(JSTaggedValue::HasProperty(thread, targetVal, symKey));
}

 /*
  * mixed layout [Symbol.iterator, "a", Symbol.toStringTag, "b"] with
  * recording-setter target must copy all four keys in C++ order — strings first
  * (slot order), then symbols (slot order).
  */
HWTEST_F_L0(SendableBuiltinsIRStressTest, AssignSharedSourceMixedLayoutCopiesStringsThenSymbols)
{
    ObjectFactory *factory = instance->GetFactory();
    JSHandle<GlobalEnv> env = instance->GetGlobalEnv();
    // Unique property values shared by construction and assertions.
    constexpr int32_t valueSymIter = 10;  // Symbol.iterator
    constexpr int32_t valueA = 1;          // "a"
    constexpr int32_t valueSymTag = 20;   // Symbol.toStringTag
    constexpr int32_t valueB = 2;          // "b"

    // Shared source with layout [Symbol.iterator, "a", Symbol.toStringTag, "b"].
    JSHandle<JSTaggedValue> symIter = thread->GlobalConstants()->GetHandledIteratorSymbol();
    JSHandle<JSTaggedValue> symTag = thread->GlobalConstants()->GetHandledToStringTagSymbol();
    JSHandle<JSTaggedValue> keyA(factory->NewFromASCII("a"));
    JSHandle<JSTaggedValue> keyB(factory->NewFromASCII("b"));
    std::vector<PropertyDescriptor> descs;
    PropertyDescriptor dSymIter(thread, true, true, true);
    dSymIter.SetKey(symIter);
    dSymIter.SetSharedFieldType(SharedFieldType::NUMBER);
    dSymIter.SetValue(JSHandle<JSTaggedValue>(thread, JSTaggedValue(valueSymIter)));
    descs.push_back(dSymIter);
    PropertyDescriptor dA(thread, true, true, true);
    dA.SetKey(keyA);
    dA.SetSharedFieldType(SharedFieldType::NUMBER);
    dA.SetValue(JSHandle<JSTaggedValue>(thread, JSTaggedValue(valueA)));
    descs.push_back(dA);
    PropertyDescriptor dSymTag(thread, true, true, true);
    dSymTag.SetKey(symTag);
    dSymTag.SetSharedFieldType(SharedFieldType::NUMBER);
    dSymTag.SetValue(JSHandle<JSTaggedValue>(thread, JSTaggedValue(valueSymTag)));
    descs.push_back(dSymTag);
    PropertyDescriptor dB(thread, true, true, true);
    dB.SetKey(keyB);
    dB.SetSharedFieldType(SharedFieldType::NUMBER);
    dB.SetValue(JSHandle<JSTaggedValue>(thread, JSTaggedValue(valueB)));
    descs.push_back(dB);
    JSHandle<JSTaggedValue> sharedSrc = factory->CreateSObjectWithProperties(descs);
    ASSERT_TRUE(sharedSrc->IsJSSharedObject());

    // Plain target whose own enumerable properties are recording accessors.
    Local<FunctionRef> setter = FunctionRef::New(instance, AssignOrderSetter);
    JSHandle<JSTaggedValue> setterHandle = JSNApiHelper::ToJSHandle(setter);
    JSHandle<JSObject> target = factory->OrdinaryNewJSObjectCreate(env->GetObjectFunctionPrototype());
    auto defineAccessor = [this, &setterHandle, &target](const JSHandle<JSTaggedValue> &key) {
        PropertyDescriptor desc(thread);
        desc.SetSetter(setterHandle);
        desc.SetEnumerable(true);
        desc.SetConfigurable(true);
        return JSTaggedValue::DefineOwnProperty(thread, JSHandle<JSTaggedValue>(target), key, desc);
    };
    ASSERT_TRUE(defineAccessor(symIter));
    ASSERT_TRUE(defineAccessor(keyA));
    ASSERT_TRUE(defineAccessor(symTag));
    ASSERT_TRUE(defineAccessor(keyB));

    // Object.assign(target, sharedSrc) through the interpreter (IR stub dispatch path).
    JSHandle<JSTaggedValue> objectFn = env->GetObjectFunction();
    JSHandle<JSTaggedValue> assignFn = JSObject::GetMethod(
        thread, objectFn, JSHandle<JSTaggedValue>(factory->NewFromASCII("assign")));
    ASSERT_TRUE(!assignFn.IsEmpty() && !assignFn->IsUndefined());
    g_assignSetterValues.clear();
    {
        EcmaHandleScope scope(thread);
        InvokeMethod(thread, assignFn, JSHandle<JSTaggedValue>(thread, JSTaggedValue::Undefined()),
                     {target.GetTaggedValue(), sharedSrc.GetTaggedValue()});
    }
    ASSERT_FALSE(thread->HasPendingException());
    // Strings in layout slot order ("a", "b"), then symbols in layout slot order
    // (Symbol.iterator, Symbol.toStringTag): all four keys, C++ order, no drop.
    constexpr size_t expectedSetterCalls = 4;
    ASSERT_EQ(g_assignSetterValues.size(), expectedSetterCalls);
    EXPECT_EQ(g_assignSetterValues[0], valueA);        // "a"
    EXPECT_EQ(g_assignSetterValues[1], valueB);        // "b"
    EXPECT_EQ(g_assignSetterValues[2], valueSymIter); // Symbol.iterator
    EXPECT_EQ(g_assignSetterValues[3], valueSymTag);  // Symbol.toStringTag
}

// Builds a JS_SHARED_OBJECT with the given @@toStringTag value, then invokes
// SharedObject.prototype.toString on it through the interpreter.
static JSTaggedValue ToStringOnSharedWithTag(EcmaVM *vm, const JSHandle<JSTaggedValue> &tagValue)
{
    JSThread *thread = vm->GetJSThread();
    ObjectFactory *factory = vm->GetFactory();
    JSHandle<GlobalEnv> env = vm->GetGlobalEnv();
    JSHandle<JSTaggedValue> tagSymbol = thread->GlobalConstants()->GetHandledToStringTagSymbol();
    std::vector<PropertyDescriptor> descs;
    PropertyDescriptor tagDesc(thread, true, true, true);
    tagDesc.SetKey(tagSymbol);
    tagDesc.SetValue(tagValue);
    descs.push_back(tagDesc);
    JSHandle<JSTaggedValue> sharedObj = factory->CreateSObjectWithProperties(descs);
    if (!sharedObj->IsJSSharedObject()) {
        return JSTaggedValue::Exception();
    }
    JSHandle<JSTaggedValue> sObjPrototype = env->GetSObjectFunctionPrototype();
    JSHandle<JSTaggedValue> toStringFn = JSObject::GetMethod(
        thread, sObjPrototype, JSHandle<JSTaggedValue>(factory->NewFromASCII("toString")));
    if (toStringFn.IsEmpty() || toStringFn->IsUndefined()) {
        return JSTaggedValue::Exception();
    }
    EcmaHandleScope scope(thread);
    return InvokeMethod(thread, toStringFn, sharedObj, {});
}

 /*
  * with an accessor @@toStringTag (getter returns
  * String), the pre-lookup invokes the getter once and the C++ slow path invokes it a
  * second time - the result comes from that second call.
  */
HWTEST_F_L0(SendableBuiltinsIRStressTest, ToStringSharedAccessorTagMatchesPreChangeBehavior)
{
    LocalScope scope(instance);
    g_toStringTagGetterCalls.store(0);
    Local<FunctionRef> getter = FunctionRef::NewSendable(instance, ToStringTagGetter, nullptr);
    Local<JSValueRef> accessor =
        ObjectRef::CreateSendableAccessorData(instance, getter, JSValueRef::Undefined(instance));

    JSTaggedValue r = ToStringOnSharedWithTag(instance, JSNApiHelper::ToJSHandle(accessor));
    ASSERT_FALSE(thread->HasPendingException());
    ASSERT_TRUE(r.IsString());
    ASSERT_EQ(g_toStringTagGetterCalls.load(), EXPECTED_TAG_GETTER_CALLS);
    JSHandle<EcmaString> expected = instance->GetFactory()->NewFromUtf8("[object Tag2]");
    ASSERT_TRUE(JSTaggedValue::StrictEqual(thread, JSHandle<JSTaggedValue>(thread, r),
                                           JSHandle<JSTaggedValue>(expected)));
}

/*
 * a data @@toStringTag on a JS_SHARED_OBJECT receiver composes the custom tag.
 */
HWTEST_F_L0(SendableBuiltinsIRStressTest, ToStringSharedDataTagComposesCustomTag)
{
    JSHandle<JSTaggedValue> tagValue(thread,
        instance->GetFactory()->NewFromUtf8("Custom").GetTaggedValue());
    JSTaggedValue r = ToStringOnSharedWithTag(instance, tagValue);
    ASSERT_FALSE(thread->HasPendingException());
    ASSERT_TRUE(r.IsString());
    JSHandle<EcmaString> expected = instance->GetFactory()->NewFromUtf8("[object Custom]");
    ASSERT_TRUE(JSTaggedValue::StrictEqual(thread, JSHandle<JSTaggedValue>(thread, r),
                                           JSHandle<JSTaggedValue>(expected)));
}

 /*
  * with an accessor @@toStringTag whose getter returns a
  * NON-string, the getter runs twice (pre-lookup + second [[Get]]) and the output
  * is "[object Object]".
  */
HWTEST_F_L0(SendableBuiltinsIRStressTest, ToStringSharedAccessorTagNonStringKeepsPreChangeBehavior)
{
    LocalScope scope(instance);
    g_nonStringTagGetterCalls.store(0);
    Local<FunctionRef> getter = FunctionRef::NewSendable(instance, NonStringTagGetter, nullptr);
    Local<JSValueRef> accessor =
        ObjectRef::CreateSendableAccessorData(instance, getter, JSValueRef::Undefined(instance));

    JSTaggedValue r = ToStringOnSharedWithTag(instance, JSNApiHelper::ToJSHandle(accessor));
    ASSERT_FALSE(thread->HasPendingException());
    ASSERT_TRUE(r.IsString());
    ASSERT_EQ(g_nonStringTagGetterCalls.load(), EXPECTED_TAG_GETTER_CALLS);
    JSHandle<EcmaString> expected = instance->GetFactory()->NewFromUtf8("[object Object]");
    ASSERT_TRUE(JSTaggedValue::StrictEqual(thread, JSHandle<JSTaggedValue>(thread, r),
                                           JSHandle<JSTaggedValue>(expected)));
}

 /*
  * an accessor @@toStringTag whose getter returns a NON-string
  * on the first call and a String on the second - the pre-lookup result is dropped and
  * the second [[Get]] result is composed.
  */
HWTEST_F_L0(SendableBuiltinsIRStressTest, ToStringSharedAccessorTagFlipKeepsPreChangeBehavior)
{
    LocalScope scope(instance);
    g_flipTagGetterCalls.store(0);
    Local<FunctionRef> getter = FunctionRef::NewSendable(instance, FlipTagGetter, nullptr);
    Local<JSValueRef> accessor =
        ObjectRef::CreateSendableAccessorData(instance, getter, JSValueRef::Undefined(instance));

    JSTaggedValue r = ToStringOnSharedWithTag(instance, JSNApiHelper::ToJSHandle(accessor));
    ASSERT_FALSE(thread->HasPendingException());
    ASSERT_TRUE(r.IsString());
    ASSERT_EQ(g_flipTagGetterCalls.load(), EXPECTED_TAG_GETTER_CALLS);
    JSHandle<EcmaString> expected = instance->GetFactory()->NewFromUtf8("[object Flip2]");
    ASSERT_TRUE(JSTaggedValue::StrictEqual(thread, JSHandle<JSTaggedValue>(thread, r),
                                           JSHandle<JSTaggedValue>(expected)));
}
}  // namespace panda::test
