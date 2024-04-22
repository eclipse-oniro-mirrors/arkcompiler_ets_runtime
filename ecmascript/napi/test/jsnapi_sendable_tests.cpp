/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ecmascript/napi/jsnapi_helper.h"
#include "ecmascript/tests/test_helper.h"
#include "gtest/gtest.h"

using namespace panda::ecmascript;

namespace panda::test {
class JSNApiTests : public testing::Test {
public:
    static void SetUpTestCase()
    {
        GTEST_LOG_(INFO) << "SetUpTestCase";
    }

    static void TearDownTestCase()
    {
        GTEST_LOG_(INFO) << "TearDownCase";
    }

    void SetUp() override
    {
        RuntimeOption option;
        option.SetLogLevel(RuntimeOption::LOG_LEVEL::ERROR);
        vm_ = JSNApi::CreateJSVM(option);
        ASSERT_TRUE(vm_ != nullptr) << "Cannot create Runtime";
        thread_ = vm_->GetJSThread();
        vm_->SetEnableForceGC(true);
        thread_->ManagedCodeBegin();
        staticKey = StringRef::NewFromUtf8(vm_, "static");
        nonStaticKey = StringRef::NewFromUtf8(vm_, "nonStatic");
        instanceKey = StringRef::NewFromUtf8(vm_, "instance");
        getterSetterKey = StringRef::NewFromUtf8(vm_, "getterSetter");
        invalidKey = StringRef::NewFromUtf8(vm_, "invalid");
    }

    void TearDown() override
    {
        thread_->ManagedCodeEnd();
        vm_->SetEnableForceGC(false);
        JSNApi::DestroyJSVM(vm_);
    }

    template <typename T>
    void TestNumberRef(T val, TaggedType expected)
    {
        LocalScope scope(vm_);
        Local<NumberRef> obj = NumberRef::New(vm_, val);
        ASSERT_TRUE(obj->IsNumber());
        JSTaggedType res = JSNApiHelper::ToJSTaggedValue(*obj).GetRawData();
        ASSERT_EQ(res, expected);
        if constexpr (std::is_floating_point_v<T>) {
            if (std::isnan(val)) {
                ASSERT_TRUE(std::isnan(obj->Value()));
            } else {
                ASSERT_EQ(obj->Value(), val);
            }
        } else if constexpr (sizeof(T) >= sizeof(int32_t)) {
            ASSERT_EQ(obj->IntegerValue(vm_), val);
        } else if constexpr (std::is_signed_v<T>) {
            ASSERT_EQ(obj->Int32Value(vm_), val);
        } else {
            ASSERT_EQ(obj->Uint32Value(vm_), val);
        }
    }

    TaggedType ConvertDouble(double val)
    {
        return base::bit_cast<JSTaggedType>(val) + JSTaggedValue::DOUBLE_ENCODE_OFFSET;
    }

protected:
    JSThread *thread_ = nullptr;
    EcmaVM *vm_ = nullptr;
    Local<StringRef> staticKey;
    Local<StringRef> nonStaticKey;
    Local<StringRef> instanceKey;
    Local<StringRef> getterSetterKey;
    Local<StringRef> invalidKey;
};

panda::JSValueRef FunctionCallback(JsiRuntimeCallInfo *info)
{
    EcmaVM *vm = info->GetVM();
    Local<JSValueRef> jsThisRef = info->GetThisRef();
    Local<ObjectRef> thisRef = jsThisRef->ToObject(vm);
    return **thisRef;
}

Local<FunctionRef> GetNewSendableClassFunction(
    EcmaVM *vm, const char *instanceKey, const char *staticKey, const char *nonStaticKey, Local<FunctionRef> parent)
{
    FunctionRef::SendablePropertiesInfos infos;

    Local<StringRef> instanceStr = StringRef::NewFromUtf8(vm, instanceKey);
    infos.instancePropertiesInfo.keys.push_back(instanceStr);
    infos.instancePropertiesInfo.types.push_back(FunctionRef::SendableType::NONE);
    infos.instancePropertiesInfo.attributes.push_back(PropertyAttribute(instanceStr, true, true, true));

    Local<StringRef> staticStr = StringRef::NewFromUtf8(vm, staticKey);
    infos.staticPropertiesInfo.keys.push_back(staticStr);
    infos.staticPropertiesInfo.types.push_back(FunctionRef::SendableType::NONE);
    infos.staticPropertiesInfo.attributes.push_back(PropertyAttribute(staticStr, true, true, true));

    Local<StringRef> nonStaticStr = StringRef::NewFromUtf8(vm, nonStaticKey);
    infos.nonStaticPropertiesInfo.keys.push_back(nonStaticStr);
    infos.nonStaticPropertiesInfo.types.push_back(FunctionRef::SendableType::NONE);
    infos.nonStaticPropertiesInfo.attributes.push_back(PropertyAttribute(nonStaticStr, true, true, true));

    Local<FunctionRef> constructor = FunctionRef::NewSendableClassFunction(
        vm, FunctionCallback, nullptr, nullptr, StringRef::NewFromUtf8(vm, "name"), infos, parent);

    return constructor;
}

HWTEST_F_L0(JSNApiTests, NewSendableClassFunction)
{
    LocalScope scope(vm_);
    Local<FunctionRef> constructor =
        GetNewSendableClassFunction(vm_, "instance", "static", "nonStatic", FunctionRef::Null(vm_));

    ASSERT_EQ("name", constructor->GetName(vm_)->ToString());
    ASSERT_TRUE(constructor->IsFunction());
    JSHandle<JSTaggedValue> jsConstructor = JSNApiHelper::ToJSHandle(constructor);
    ASSERT_TRUE(jsConstructor->IsClassConstructor());

    Local<JSValueRef> functionPrototype = constructor->GetFunctionPrototype(vm_);
    ASSERT_TRUE(functionPrototype->IsObject());
    JSHandle<JSTaggedValue> jsPrototype = JSNApiHelper::ToJSHandle(functionPrototype);
    ASSERT_TRUE(jsPrototype->IsClassPrototype());

    const GlobalEnvConstants *globalConst = thread_->GlobalConstants();
    auto prototype = JSTaggedValue::GetProperty(
                         thread_, JSNApiHelper::ToJSHandle(constructor), globalConst->GetHandledPrototypeString())
                         .GetValue();
    ASSERT_FALSE(prototype->IsUndefined());
    ASSERT_TRUE(prototype->IsECMAObject() || prototype->IsNull());
}

HWTEST_F_L0(JSNApiTests, NewSendableClassFunctionProperties)
{
    LocalScope scope(vm_);
    Local<FunctionRef> constructor =
        GetNewSendableClassFunction(vm_, "instance", "static", "nonStatic", FunctionRef::Null(vm_));
    Local<ObjectRef> prototype = constructor->GetFunctionPrototype(vm_);

    ASSERT_EQ("static", constructor->Get(vm_, staticKey)->ToString(vm_)->ToString());
    ASSERT_EQ("nonStatic", prototype->Get(vm_, nonStaticKey)->ToString(vm_)->ToString());
    ASSERT_EQ("undefined", constructor->Get(vm_, invalidKey)->ToString(vm_)->ToString());
    ASSERT_EQ("undefined", prototype->Get(vm_, invalidKey)->ToString(vm_)->ToString());

    // set static property on constructor
    constructor->Set(vm_, staticKey, StringRef::NewFromUtf8(vm_, "static0"));
    ASSERT_EQ("static0", constructor->Get(vm_, staticKey)->ToString(vm_)->ToString());

    // set non static property on prototype
    prototype->Set(vm_, nonStaticKey, StringRef::NewFromUtf8(vm_, "nonStatic0"));
    ASSERT_EQ("nonStatic0", prototype->Get(vm_, nonStaticKey)->ToString(vm_)->ToString());

    // set invalid property on constructor
    ASSERT_FALSE(vm_->GetJSThread()->HasPendingException());
    constructor->Set(vm_, invalidKey, StringRef::NewFromUtf8(vm_, "invalid"));
    ASSERT_TRUE(vm_->GetJSThread()->HasPendingException());
    JSNApi::GetAndClearUncaughtException(vm_);
    ASSERT_EQ("undefined", constructor->Get(vm_, invalidKey)->ToString(vm_)->ToString());

    // set invalid property on prototype
    ASSERT_FALSE(vm_->GetJSThread()->HasPendingException());
    prototype->Set(vm_, invalidKey, StringRef::NewFromUtf8(vm_, "invalid"));
    ASSERT_TRUE(vm_->GetJSThread()->HasPendingException());
    JSNApi::GetAndClearUncaughtException(vm_);
    ASSERT_EQ("undefined", prototype->Get(vm_, invalidKey)->ToString(vm_)->ToString());
}

HWTEST_F_L0(JSNApiTests, NewSendableClassFunctionInstance)
{
    LocalScope scope(vm_);
    Local<FunctionRef> constructor =
        GetNewSendableClassFunction(vm_, "instance", "static", "nonStatic", FunctionRef::Null(vm_));
    Local<JSValueRef> argv[1] = {NumberRef::New(vm_, 0)};
    Local<ObjectRef> obj = constructor->Constructor(vm_, argv, 0);
    Local<ObjectRef> obj0 = constructor->Constructor(vm_, argv, 0);

    ASSERT_TRUE(JSFunction::InstanceOf(thread_, JSNApiHelper::ToJSHandle(obj), JSNApiHelper::ToJSHandle(constructor)));
    ASSERT_TRUE(JSFunction::InstanceOf(thread_, JSNApiHelper::ToJSHandle(obj0), JSNApiHelper::ToJSHandle(constructor)));

    // set instance property
    ASSERT_EQ("undefined", obj->Get(vm_, instanceKey)->ToString(vm_)->ToString());
    ASSERT_EQ("undefined", obj0->Get(vm_, instanceKey)->ToString(vm_)->ToString());
    obj->Set(vm_, instanceKey, StringRef::NewFromUtf8(vm_, "instance"));
    ASSERT_EQ("instance", obj->Get(vm_, instanceKey)->ToString(vm_)->ToString());
    ASSERT_EQ("undefined", obj0->Get(vm_, instanceKey)->ToString(vm_)->ToString());

    // set non static property on prototype and get from instance
    ASSERT_EQ("nonStatic", obj->Get(vm_, nonStaticKey)->ToString(vm_)->ToString());
    ASSERT_EQ("nonStatic", obj0->Get(vm_, nonStaticKey)->ToString(vm_)->ToString());
    Local<ObjectRef> prototype = obj->GetPrototype(vm_);
    prototype->Set(vm_, nonStaticKey, StringRef::NewFromUtf8(vm_, "nonStatic0"));
    ASSERT_EQ("nonStatic0", obj->Get(vm_, nonStaticKey)->ToString(vm_)->ToString());
    ASSERT_EQ("nonStatic0", obj0->Get(vm_, nonStaticKey)->ToString(vm_)->ToString());

    // set non static property on instance
    ASSERT_FALSE(vm_->GetJSThread()->HasPendingException());
    obj->Set(vm_, nonStaticKey, StringRef::NewFromUtf8(vm_, "nonStatic1"));
    ASSERT_TRUE(vm_->GetJSThread()->HasPendingException());
    JSNApi::GetAndClearUncaughtException(vm_);
    ASSERT_EQ("nonStatic0", obj->Get(vm_, nonStaticKey)->ToString(vm_)->ToString());
    ASSERT_EQ("nonStatic0", obj0->Get(vm_, nonStaticKey)->ToString(vm_)->ToString());

    // set invalid property on instance
    ASSERT_EQ("undefined", obj->Get(vm_, invalidKey)->ToString(vm_)->ToString());
    ASSERT_FALSE(vm_->GetJSThread()->HasPendingException());
    obj->Set(vm_, invalidKey, StringRef::NewFromUtf8(vm_, "invalid"));
    ASSERT_TRUE(vm_->GetJSThread()->HasPendingException());
    JSNApi::GetAndClearUncaughtException(vm_);
    ASSERT_EQ("undefined", obj->Get(vm_, invalidKey)->ToString(vm_)->ToString());
}

HWTEST_F_L0(JSNApiTests, NewSendableClassFunctionInherit)
{
    LocalScope scope(vm_);
    Local<FunctionRef> parent =
        GetNewSendableClassFunction(vm_, "parentInstance", "parentStatic", "parentNonStatic", FunctionRef::Null(vm_));
    Local<FunctionRef> constructor = GetNewSendableClassFunction(vm_, "instance", "static", "nonStatic", parent);
    Local<JSValueRef> argv[1] = {NumberRef::New(vm_, 0)};
    Local<ObjectRef> obj = constructor->Constructor(vm_, argv, 0);
    Local<ObjectRef> obj0 = constructor->Constructor(vm_, argv, 0);

    ASSERT_TRUE(JSFunction::InstanceOf(thread_, JSNApiHelper::ToJSHandle(obj), JSNApiHelper::ToJSHandle(parent)));
    ASSERT_TRUE(JSFunction::InstanceOf(thread_, JSNApiHelper::ToJSHandle(obj0), JSNApiHelper::ToJSHandle(parent)));

    // set parent instance property on instance
    Local<StringRef> parentInstanceKey = StringRef::NewFromUtf8(vm_, "parentInstance");
    ASSERT_EQ("undefined", obj->Get(vm_, parentInstanceKey)->ToString(vm_)->ToString());
    ASSERT_FALSE(vm_->GetJSThread()->HasPendingException());
    obj->Set(vm_, parentInstanceKey, StringRef::NewFromUtf8(vm_, "parentInstance"));
    ASSERT_TRUE(vm_->GetJSThread()->HasPendingException());
    JSNApi::GetAndClearUncaughtException(vm_);
    ASSERT_EQ("undefined", obj->Get(vm_, parentInstanceKey)->ToString(vm_)->ToString());

    // get parent static property from constructor
    Local<StringRef> parentStaticKey = StringRef::NewFromUtf8(vm_, "parentStatic");
    ASSERT_EQ("parentStatic", constructor->Get(vm_, parentStaticKey)->ToString(vm_)->ToString());

    // get parent non static property form instance
    Local<StringRef> parentNonStaticKey = StringRef::NewFromUtf8(vm_, "parentNonStatic");
    ASSERT_EQ("parentNonStatic", obj->Get(vm_, parentNonStaticKey)->ToString(vm_)->ToString());
}

HWTEST_F_L0(JSNApiTests, NewSendable)
{
    LocalScope scope(vm_);
    Local<FunctionRef> func = FunctionRef::NewSendable(
        vm_,
        [](JsiRuntimeCallInfo *runtimeInfo) -> JSValueRef {
            EcmaVM *vm = runtimeInfo->GetVM();
            LocalScope scope(vm);
            return **StringRef::NewFromUtf8(vm, "funcResult");
        },
        nullptr);
    Local<JSValueRef> res = func->Call(vm_, JSValueRef::Undefined(vm_), nullptr, 0);
    ASSERT_EQ("funcResult", res->ToString(vm_)->ToString());
}

HWTEST_F_L0(JSNApiTests, NewSendableClassFunctionFunction)
{
    LocalScope scope(vm_);

    FunctionRef::SendablePropertiesInfos infos;

    Local<FunctionRef> func = FunctionRef::NewSendable(
        vm_,
        [](JsiRuntimeCallInfo *runtimeInfo) -> JSValueRef {
            EcmaVM *vm = runtimeInfo->GetVM();
            LocalScope scope(vm);
            return **StringRef::NewFromUtf8(vm, "funcResult");
        },
        nullptr);
    infos.staticPropertiesInfo.keys.push_back(staticKey);
    infos.staticPropertiesInfo.types.push_back(FunctionRef::SendableType::OBJECT);
    infos.staticPropertiesInfo.attributes.push_back(PropertyAttribute(func, true, true, true));

    Local<FunctionRef> constructor = FunctionRef::NewSendableClassFunction(
        vm_, FunctionCallback, nullptr, nullptr, StringRef::NewFromUtf8(vm_, "name"), infos, FunctionRef::Null(vm_));

    Local<FunctionRef> staticValue = constructor->Get(vm_, staticKey);
    ASSERT_TRUE(staticValue->IsFunction());
    Local<JSValueRef> res = staticValue->Call(vm_, JSValueRef::Undefined(vm_), nullptr, 0);
    ASSERT_EQ("funcResult", res->ToString(vm_)->ToString());
}

HWTEST_F_L0(JSNApiTests, NewSendableClassFunctionGetterSetter)
{
    LocalScope scope(vm_);

    FunctionRef::SendablePropertiesInfos infos;

    Local<StringRef> getterSetter = StringRef::NewFromUtf8(vm_, "getterSetter");
    infos.staticPropertiesInfo.keys.push_back(getterSetter);
    infos.staticPropertiesInfo.types.push_back(FunctionRef::SendableType::NONE);
    infos.staticPropertiesInfo.attributes.push_back(PropertyAttribute(getterSetter, true, true, true));
    Local<FunctionRef> staticGetter = FunctionRef::NewSendable(
        vm_,
        [](JsiRuntimeCallInfo *info) -> JSValueRef {
            Local<JSValueRef> value = info->GetThisRef();
            Local<ObjectRef> obj = value->ToObject(info->GetVM());
            Local<JSValueRef> temp = obj->Get(info->GetVM(), StringRef::NewFromUtf8(info->GetVM(), "getterSetter"));
            return **temp->ToString(info->GetVM());
        },
        nullptr);
    Local<FunctionRef> staticSetter = FunctionRef::NewSendable(
        vm_,
        [](JsiRuntimeCallInfo *info) -> JSValueRef {
            Local<JSValueRef> arg = info->GetCallArgRef(0);
            Local<JSValueRef> value = info->GetThisRef();
            Local<ObjectRef> obj = value->ToObject(info->GetVM());
            obj->Set(info->GetVM(), StringRef::NewFromUtf8(info->GetVM(), "getterSetter"), arg);
            return **JSValueRef::Undefined(info->GetVM());
        },
        nullptr);
    Local<JSValueRef> staticValue = panda::ObjectRef::CreateSendableAccessorData(vm_, staticGetter, staticSetter);
    infos.staticPropertiesInfo.keys.push_back(staticKey);
    infos.staticPropertiesInfo.types.push_back(FunctionRef::SendableType::OBJECT);
    infos.staticPropertiesInfo.attributes.push_back(PropertyAttribute(staticValue, true, true, true));

    Local<FunctionRef> constructor = FunctionRef::NewSendableClassFunction(
        vm_, FunctionCallback, nullptr, nullptr, StringRef::NewFromUtf8(vm_, "name"), infos, FunctionRef::Null(vm_));

    ASSERT_EQ("getterSetter", constructor->Get(vm_, getterSetter)->ToString(vm_)->ToString());
    ASSERT_EQ("getterSetter", constructor->Get(vm_, staticKey)->ToString(vm_)->ToString());
    constructor->Set(vm_, staticKey, StringRef::NewFromUtf8(vm_, "getterSetter0"));
    ASSERT_EQ("getterSetter0", constructor->Get(vm_, getterSetter)->ToString(vm_)->ToString());
    ASSERT_EQ("getterSetter0", constructor->Get(vm_, staticKey)->ToString(vm_)->ToString());
}

}  // namespace panda::test
