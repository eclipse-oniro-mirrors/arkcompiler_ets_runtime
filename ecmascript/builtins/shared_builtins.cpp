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

#include "ecmascript/builtins/builtins.h"

#include "ecmascript/builtins/builtins_function.h"
#include "ecmascript/builtins/builtins_object.h"
#include "ecmascript/builtins/builtins_symbol.h"
#include "ecmascript/builtins/builtins_shared_function.h"
#include "ecmascript/builtins/builtins_shared_object.h"
#include "ecmascript/builtins/builtins_shared_map.h"
#include "ecmascript/builtins/builtins_shared_set.h"
#include "ecmascript/shared_objects/js_shared_array.h"
#include "ecmascript/shared_objects/js_shared_map.h"
#include "ecmascript/shared_objects/js_shared_object.h"
#include "ecmascript/shared_objects/js_shared_set.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/symbol_table.h"
#include "ecmascript/builtins/builtins_shared_array.h"

namespace panda::ecmascript {
using BuiltinsSharedObject = builtins::BuiltinsSharedObject;
using BuiltinsSharedFunction = builtins::BuiltinsSharedFunction;
using Function = builtins::BuiltinsFunction;
using Object = builtins::BuiltinsObject;
using BuiltinsSharedSet = builtins::BuiltinsSharedSet;
using BuiltinsSharedMap = builtins::BuiltinsSharedMap;
using BuiltinsSharedArray = builtins::BuiltinsSharedArray;

void Builtins::InitializeSObjectAndSFunction(const JSHandle<GlobalEnv> &env) const
{
    [[maybe_unused]] EcmaHandleScope scope(thread_);
    // SharedObject.prototype[hclass]
    JSHandle<JSHClass> sobjPrototypeHClass = CreateSObjectPrototypeHClass();
    // SharedObject.prototype
    JSHandle<JSObject> sObjPrototype =
        factory_->NewSharedOldSpaceJSObject(sobjPrototypeHClass);
    JSHandle<JSTaggedValue> sObjPrototypeVal(sObjPrototype);
    // SharedObject.prototype_or_hclass
    auto emptySLayout = thread_->GlobalConstants()->GetHandledEmptySLayoutInfo();
    JSHandle<JSHClass> sObjIHClass =
        factory_->NewSEcmaHClass(JSSharedObject::SIZE, 0, JSType::JS_SHARED_OBJECT, sObjPrototypeVal,
                                 emptySLayout);
    // SharedFunction.prototype_or_hclass
    JSHandle<JSHClass> sFuncPrototypeHClass = CreateSFunctionPrototypeHClass(sObjPrototypeVal);
    // SharedFunction.prototype
    JSHandle<JSFunction> sFuncPrototype = factory_->NewSFunctionByHClass(
        reinterpret_cast<void *>(Function::FunctionPrototypeInvokeSelf), sFuncPrototypeHClass,
        FunctionKind::NORMAL_FUNCTION);
    InitializeSFunction(env, sFuncPrototype);
    InitializeSObject(env, sObjIHClass, sObjPrototype, sFuncPrototype);
    InitializeSSet(env, sObjPrototype, sFuncPrototype);
    InitializeSMap(env, sObjPrototype, sFuncPrototype);
    InitializeSharedArray(env, sObjPrototype, sFuncPrototype);
    env->SetSObjectFunctionPrototype(thread_, sObjPrototype);
}

void Builtins::CopySObjectAndSFunction(const JSHandle<GlobalEnv> &env, const JSTaggedValue &srcEnv) const
{
    // Copy shareds.
    ASSERT(srcEnv.IsJSGlobalEnv());
    auto sGlobalEnv = reinterpret_cast<GlobalEnv*>(srcEnv.GetTaggedObject());
#define COPY_ENV_SHARED_FIELDS(Type, Name, INDEX)    \
    env->Set##Name(thread_, sGlobalEnv->Get##Name());
    GLOBAL_ENV_SHARED_FIELDS(COPY_ENV_SHARED_FIELDS)
#undef COPY_ENV_SHARED_FIELDS
}

void Builtins::InitializeSObject(const JSHandle<GlobalEnv> &env, const JSHandle<JSHClass> &sObjIHClass,
                                 const JSHandle<JSObject> &sObjPrototype,
                                 const JSHandle<JSFunction> &sFuncPrototype) const
{
    [[maybe_unused]] EcmaHandleScope scope(thread_);
    // SharedObject constructor (forbidden use NewBuiltinConstructor)
    JSHandle<JSHClass> sObjectFunctionHClass = CreateSObjectFunctionHClass(sFuncPrototype);
    JSHandle<JSFunction> sObjectFunction =
        factory_->NewSFunctionByHClass(reinterpret_cast<void *>(BuiltinsSharedObject::SharedObjectConstructor),
                                       sObjectFunctionHClass, FunctionKind::BUILTIN_CONSTRUCTOR);

    InitializeSCtor(sObjIHClass, sObjectFunction, "SharedObject", FunctionLength::ONE);
    env->SetSObjectFunction(thread_, sObjectFunction);
    // sObject method.
    uint32_t fieldIndex = JSFunction::PROTOTYPE_INLINE_PROPERTY_INDEX + 1;
    for (const base::BuiltinFunctionEntry &entry : Object::GetObjectFunctions()) {
        SetSFunction(env, JSHandle<JSObject>(sObjectFunction), entry.GetName(), entry.GetEntrypoint(),
                     fieldIndex++, entry.GetLength(), entry.GetBuiltinStubId());
    }
    // sObject.prototype method
    fieldIndex = 0; // constructor
    sObjPrototype->SetPropertyInlinedProps(thread_, fieldIndex++, sObjectFunction.GetTaggedValue());
    for (const base::BuiltinFunctionEntry &entry : Object::GetObjectPrototypeFunctions()) {
        SetSFunction(env, sObjPrototype, entry.GetName(), entry.GetEntrypoint(), fieldIndex++, entry.GetLength(),
                     entry.GetBuiltinStubId());
    }
    // B.2.2.1 sObject.prototype.__proto__
    JSHandle<JSTaggedValue> protoGetter =
        CreateSGetterSetter(env, Object::ProtoGetter, "__proto__", FunctionLength::ZERO);
    JSHandle<JSTaggedValue> protoSetter =
        CreateSGetterSetter(env, Object::ProtoSetter, "__proto__", FunctionLength::ONE);
    SetSAccessor(sObjPrototype, fieldIndex, protoGetter, protoSetter);
}

void Builtins::InitializeSSet(const JSHandle<GlobalEnv> &env, const JSHandle<JSObject> &sObjPrototype,
    const JSHandle<JSFunction> &sFuncPrototype) const
{
    [[maybe_unused]] EcmaHandleScope scope(thread_);
    const GlobalEnvConstants *globalConst = thread_->GlobalConstants();
    // SharedSet.prototype
    JSHandle<JSHClass> setPrototypeHClass = CreateSSetPrototypeHClass(sObjPrototype);
    JSHandle<JSObject> setPrototype =
        factory_->NewSharedOldSpaceJSObjectWithInit(setPrototypeHClass);

    JSHandle<JSTaggedValue> setPrototypeValue(setPrototype);
    // SharedSet.prototype_or_hclass
    auto emptySLayout = globalConst->GetHandledEmptySLayoutInfo();
    JSHandle<JSHClass> setIHClass =
        factory_->NewSEcmaHClass(JSSharedSet::SIZE, 0, JSType::JS_SHARED_SET, setPrototypeValue, emptySLayout);
    // SharedSet.hclass
    JSHandle<JSHClass> setFuncHClass = CreateSSetFunctionHClass(sFuncPrototype);
    // SharedSet() = new Function()
    JSHandle<JSFunction> setFunction =
        factory_->NewSFunctionByHClass(reinterpret_cast<void *>(BuiltinsSharedSet::Constructor),
                                       setFuncHClass, FunctionKind::BUILTIN_CONSTRUCTOR);

    InitializeSCtor(setIHClass, setFunction, "SharedSet", FunctionLength::ZERO);
    JSHandle<JSObject> globalObject(thread_, env->GetGlobalObject());
    JSHandle<JSTaggedValue> nameString(factory_->NewFromUtf8("SharedSet"));
    PropertyDescriptor desc(thread_, JSHandle<JSTaggedValue>::Cast(setFunction), true, false, true);
    JSObject::DefineOwnProperty(thread_, globalObject, nameString, desc);
    RETURN_IF_ABRUPT_COMPLETION(thread_);

    // "constructor" property on the prototype
    uint32_t fieldIndex = 0; // constructor
    setPrototype->SetPropertyInlinedProps(thread_, fieldIndex++, setFunction.GetTaggedValue());

    // SharedSet.prototype functions, excluding keys()
    for (const base::BuiltinFunctionEntry &entry: BuiltinsSharedSet::GetSetPrototypeFunctions()) {
        SetSFunction(env, setPrototype, entry.GetName(), entry.GetEntrypoint(), fieldIndex++,
                    entry.GetLength(), entry.GetBuiltinStubId());
    }
    // SharedSet.prototype.keys, which is strictly equal to Set.prototype.values
    JSHandle<JSTaggedValue> keys(factory_->NewFromASCII("keys"));
    JSHandle<JSTaggedValue> values(factory_->NewFromASCII("values"));
    JSHandle<JSTaggedValue> valuesFunc =
        JSObject::GetMethod(thread_, JSHandle<JSTaggedValue>::Cast(setPrototype), values);
    RETURN_IF_ABRUPT_COMPLETION(thread_);
    setPrototype->SetPropertyInlinedProps(thread_, fieldIndex++, valuesFunc.GetTaggedValue());

    // @@ToStringTag
    JSHandle<JSTaggedValue> strTag(factory_->NewFromUtf8("SharedSet"));
    setPrototype->SetPropertyInlinedProps(thread_, fieldIndex++, strTag.GetTaggedValue());

    // 23.1.3.10get SharedSet.prototype.size
    JSHandle<JSTaggedValue> sizeGetter = CreateSGetterSetter(env, BuiltinsSharedSet::GetSize, "size",
        FunctionLength::ZERO);
    SetSAccessor(setPrototype, fieldIndex++, sizeGetter, globalConst->GetHandledUndefined());

    // %SetPrototype% [ @@iterator ]
    setPrototype->SetPropertyInlinedProps(thread_, fieldIndex++, valuesFunc.GetTaggedValue());

    fieldIndex = JSFunction::PROTOTYPE_INLINE_PROPERTY_INDEX + 1;
    // 23.1.2.2get SharedSet [ @@species ]
    JSHandle<JSTaggedValue> speciesGetter =
        CreateSGetterSetter(env, BuiltinsSharedSet::Species, "[Symbol.species]", FunctionLength::ZERO);
    SetSAccessor(JSHandle<JSObject>(setFunction), fieldIndex, speciesGetter, globalConst->GetHandledUndefined());

    env->SetSharedSetPrototype(thread_, setPrototype);
    env->SetSBuiltininSetFunction(thread_, setFunction);
}

void Builtins::InitializeSMap(const JSHandle<GlobalEnv> &env, const JSHandle<JSObject> &sObjPrototype,
    const JSHandle<JSFunction> &sFuncPrototype) const
{
    [[maybe_unused]] EcmaHandleScope scope(thread_);
    const GlobalEnvConstants *globalConst = thread_->GlobalConstants();
    // SharedMap.prototype
    JSHandle<JSHClass> mapPrototypeHClass = CreateSMapPrototypeHClass(sObjPrototype);
    JSHandle<JSObject> mapPrototype =
        factory_->NewSharedOldSpaceJSObjectWithInit(mapPrototypeHClass);
    JSHandle<JSTaggedValue> mapPrototypeValue(mapPrototype);
    // SharedMap.prototype_or_hclass
    auto emptySLayout = globalConst->GetHandledEmptySLayoutInfo();
    JSHandle<JSHClass> mapIHClass =
        factory_->NewSEcmaHClass(JSSharedMap::SIZE, 0, JSType::JS_SHARED_MAP, mapPrototypeValue, emptySLayout);
    // SharedMap.hclass
    JSHandle<JSHClass> mapFuncHClass = CreateSMapFunctionHClass(sFuncPrototype);
    // SharedMap() = new Function()
    JSHandle<JSFunction> mapFunction =
        factory_->NewSFunctionByHClass(reinterpret_cast<void *>(BuiltinsSharedMap::Constructor),
                                       mapFuncHClass, FunctionKind::BUILTIN_CONSTRUCTOR);
    InitializeSCtor(mapIHClass, mapFunction, "SharedMap", FunctionLength::ZERO);
    JSHandle<JSObject> globalObject(thread_, env->GetGlobalObject());
    JSHandle<JSTaggedValue> nameString(factory_->NewFromUtf8("SharedMap"));
    PropertyDescriptor desc(thread_, JSHandle<JSTaggedValue>::Cast(mapFunction), true, false, true);
    JSObject::DefineOwnProperty(thread_, globalObject, nameString, desc);
    RETURN_IF_ABRUPT_COMPLETION(thread_);

    // "constructor" property on the prototype
    uint32_t fieldIndex = 0; // constructor
    mapPrototype->SetPropertyInlinedProps(thread_, fieldIndex++, mapFunction.GetTaggedValue());
    // SharedMap.prototype functions
    for (const base::BuiltinFunctionEntry &entry: BuiltinsSharedMap::GetMapPrototypeFunctions()) {
        SetSFunction(env, mapPrototype, entry.GetName(), entry.GetEntrypoint(), fieldIndex++,
                     entry.GetLength(), entry.GetBuiltinStubId());
    }
    // @@ToStringTag
    JSHandle<JSTaggedValue> strTag(factory_->NewFromUtf8("SharedMap"));
    mapPrototype->SetPropertyInlinedProps(thread_, fieldIndex++, strTag.GetTaggedValue());

    // 23.1.3.10get SharedMap.prototype.size
    JSHandle<JSTaggedValue> sizeGetter = CreateSGetterSetter(env, BuiltinsSharedMap::GetSize, "size",
        FunctionLength::ZERO);
    SetSAccessor(mapPrototype, fieldIndex++, sizeGetter, globalConst->GetHandledUndefined());

    // %MapPrototype% [ @@iterator ]
    JSHandle<JSTaggedValue> entries(factory_->NewFromASCII("entries"));
    JSHandle<JSTaggedValue> entriesFunc =
        JSObject::GetMethod(thread_, JSHandle<JSTaggedValue>::Cast(mapPrototype), entries);
    RETURN_IF_ABRUPT_COMPLETION(thread_);
    mapPrototype->SetPropertyInlinedProps(thread_, fieldIndex++, entriesFunc.GetTaggedValue());

    fieldIndex = JSFunction::PROTOTYPE_INLINE_PROPERTY_INDEX + 1;
    // 23.1.2.2get SharedMap [ @@species ]
    JSHandle<JSTaggedValue> speciesGetter =
        CreateSGetterSetter(env, BuiltinsSharedMap::Species, "[Symbol.species]", FunctionLength::ZERO);
    SetSAccessor(JSHandle<JSObject>(mapFunction), fieldIndex, speciesGetter, globalConst->GetHandledUndefined());

    env->SetSharedMapPrototype(thread_, mapPrototype);
    env->SetSBuiltininMapFunction(thread_, mapFunction);
}

void Builtins::InitializeSFunction(const JSHandle<GlobalEnv> &env,
                                   const JSHandle<JSFunction> &sFuncPrototype) const
{
    [[maybe_unused]] EcmaHandleScope scope(thread_);
    SetSFunctionLength(sFuncPrototype, FunctionLength::ZERO);
    SetSFunctionName(sFuncPrototype, thread_->GlobalConstants()->GetHandledEmptyString());
    // SharedFunction.prototype_or_hclass
    auto emptySLayout = thread_->GlobalConstants()->GetHandledEmptySLayoutInfo();
    JSHandle<JSHClass> sFuncIHClass = factory_->NewSEcmaHClass(JSSharedFunction::SIZE, 0, JSType::JS_SHARED_FUNCTION,
        JSHandle<JSTaggedValue>(sFuncPrototype), emptySLayout);
    sFuncIHClass->SetCallable(true);
    sFuncIHClass->SetConstructor(true);
    // SharedFunction.hclass
    JSHandle<JSHClass> sFuncHClass = CreateSFunctionHClass(sFuncPrototype);
    // new SharedFunction() (forbidden use NewBuiltinConstructor)
    JSHandle<JSFunction> sFuncFunction = factory_->NewSFunctionByHClass(
        reinterpret_cast<void *>(BuiltinsSharedFunction::SharedFunctionConstructor),
        sFuncHClass, FunctionKind::BUILTIN_CONSTRUCTOR);
    InitializeSCtor(sFuncIHClass, sFuncFunction, "SharedFunction", FunctionLength::ONE);
    env->SetSFunctionFunction(thread_, sFuncFunction);
    env->SetSFunctionPrototype(thread_, sFuncPrototype);

    JSHandle<JSTaggedValue> sFuncPrototypeVal(sFuncPrototype);
    JSHandle<JSHClass> functionClass =
        factory_->CreateSFunctionClass(JSSharedFunction::SIZE, JSType::JS_SHARED_FUNCTION,
                                       sFuncPrototypeVal);
    env->SetSFunctionClassWithoutProto(thread_, functionClass);

    JSHandle<JSHClass> functionClassWithoutAccessor =
        factory_->CreateSFunctionClass(JSSharedFunction::SIZE, JSType::JS_SHARED_FUNCTION,
                                       sFuncPrototypeVal, false);
    env->SetSFunctionClassWithoutAccessor(thread_, functionClassWithoutAccessor);
    uint32_t fieldIndex = 2; // 2: length and name
    JSHandle<JSObject> sFuncPrototypeObj(sFuncPrototype);
    sFuncPrototypeObj->SetPropertyInlinedProps(thread_, fieldIndex++, sFuncFunction.GetTaggedValue()); // constructor
    SharedStrictModeForbiddenAccessCallerArguments(env, fieldIndex, sFuncPrototypeObj);
    // Function.prototype method
    // 19.2.3.1 Function.prototype.apply ( thisArg, argArray )
    SetSFunction(env, sFuncPrototypeObj, "apply", Function::FunctionPrototypeApply, fieldIndex++, FunctionLength::TWO,
        BUILTINS_STUB_ID(FunctionPrototypeApply));
    // 19.2.3.2 Function.prototype.bind ( thisArg , ...args)
    SetSFunction(env, sFuncPrototypeObj, "bind", Function::FunctionPrototypeBind, fieldIndex++, FunctionLength::ONE);
    // 19.2.3.3 Function.prototype.call (thisArg , ...args)
    SetSFunction(env, sFuncPrototypeObj, "call", Function::FunctionPrototypeCall, fieldIndex++, FunctionLength::ONE);
    // 19.2.3.5 Function.prototype.toString ( )
    SetSFunction(env, sFuncPrototypeObj, thread_->GlobalConstants()->GetHandledToStringString(),
        Function::FunctionPrototypeToString, fieldIndex++, FunctionLength::ZERO);
    SetSFunction(env, sFuncPrototypeObj, "[Symbol.hasInstance]",
        Function::FunctionPrototypeHasInstance, fieldIndex++, FunctionLength::ONE);
}

JSHandle<JSHClass> Builtins::CreateSObjectFunctionHClass(const JSHandle<JSFunction> &sFuncPrototype) const
{
    uint32_t index = 0;
    PropertyAttributes attributes = PropertyAttributes::Default(false, false, false);
    attributes.SetIsInlinedProps(true);
    attributes.SetRepresentation(Representation::TAGGED);
    auto properties = Object::GetFunctionProperties();
    uint32_t length = properties.size();
    JSHandle<LayoutInfo> layout = factory_->CreateSLayoutInfo(length);
    for (const std::pair<std::string_view, bool> &each : properties) {
        attributes.SetOffset(index);
        attributes.SetIsAccessor(each.second);
        JSHandle<JSTaggedValue> keyString(factory_->NewFromUtf8(each.first));
        layout->AddKey(thread_, index++, keyString.GetTaggedValue(), attributes);
    }
    JSHandle<JSHClass> sobjPrototypeHClass =
        factory_->NewSEcmaHClass(JSSharedFunction::SIZE, length, JSType::JS_SHARED_FUNCTION,
                                 JSHandle<JSTaggedValue>(sFuncPrototype), JSHandle<JSTaggedValue>(layout));
    sobjPrototypeHClass->SetConstructor(true);
    sobjPrototypeHClass->SetCallable(true);
    return sobjPrototypeHClass;
}

JSHandle<JSHClass> Builtins::CreateSObjectPrototypeHClass() const
{
    const GlobalEnvConstants *globalConst = thread_->GlobalConstants();
    JSHandle<JSTaggedValue> nullHandle = globalConst->GetHandledNull();

    uint32_t index = 0;
    PropertyAttributes attributes = PropertyAttributes::Default(false, false, false);
    attributes.SetIsInlinedProps(true);
    attributes.SetRepresentation(Representation::TAGGED);
    auto properties = Object::GetFunctionPrototypeProperties();
    uint32_t length = properties.size();
    JSHandle<LayoutInfo> layout = factory_->CreateSLayoutInfo(length);
    for (const std::pair<std::string_view, bool> &each : properties) {
        attributes.SetOffset(index);
        attributes.SetIsAccessor(each.second);
        JSHandle<JSTaggedValue> keyString(factory_->NewFromUtf8(each.first));
        layout->AddKey(thread_, index++, keyString.GetTaggedValue(), attributes);
    }

    JSHandle<JSHClass> sobjPrototypeHClass =
        factory_->NewSEcmaHClass(JSSharedObject::SIZE, length, JSType::JS_SHARED_OBJECT, nullHandle,
                                 JSHandle<JSTaggedValue>(layout));
    return sobjPrototypeHClass;
}

JSHandle<JSHClass> Builtins::CreateSFunctionHClass(const JSHandle<JSFunction> &sFuncPrototype) const
{
    uint32_t index = 0;
    PropertyAttributes attributes = PropertyAttributes::Default(false, false, false);
    attributes.SetIsInlinedProps(true);
    attributes.SetRepresentation(Representation::TAGGED);
    auto properties = Function::GetFunctionProperties();
    uint32_t length = properties.size();
    JSHandle<LayoutInfo> layout = factory_->CreateSLayoutInfo(length);
    for (const std::pair<std::string_view, bool> &each : properties) {
        attributes.SetOffset(index);
        attributes.SetIsAccessor(each.second);
        JSHandle<JSTaggedValue> keyString(factory_->NewFromUtf8(each.first));
        layout->AddKey(thread_, index++, keyString.GetTaggedValue(), attributes);
    }
    JSHandle<JSHClass> sobjPrototypeHClass =
        factory_->NewSEcmaHClass(JSSharedFunction::SIZE, length, JSType::JS_SHARED_FUNCTION,
                                 JSHandle<JSTaggedValue>(sFuncPrototype), JSHandle<JSTaggedValue>(layout));
    sobjPrototypeHClass->SetConstructor(true);
    sobjPrototypeHClass->SetCallable(true);
    return sobjPrototypeHClass;
}

JSHandle<JSHClass> Builtins::CreateSSetFunctionHClass(const JSHandle<JSFunction> &sFuncPrototype) const
{
    uint32_t index = 0;
    auto env = vm_->GetGlobalEnv();
    PropertyAttributes attributes = PropertyAttributes::Default(false, false, false);
    attributes.SetIsInlinedProps(true);
    attributes.SetRepresentation(Representation::TAGGED);
    auto properties = BuiltinsSharedSet::GetFunctionProperties();
    uint32_t length = properties.size();
    JSHandle<JSTaggedValue> keyString;
    JSHandle<LayoutInfo> layout = factory_->CreateSLayoutInfo(length);
    for (const auto &[key, isAccessor] : properties) {
        attributes.SetOffset(index);
        attributes.SetIsAccessor(isAccessor);
        if (key == "[Symbol.species]") {
            keyString = env->GetSpeciesSymbol();
        } else {
            keyString = JSHandle<JSTaggedValue>(factory_->NewFromUtf8(key));
        }
        layout->AddKey(thread_, index++, keyString.GetTaggedValue(), attributes);
    }
    JSHandle<JSHClass> sobjPrototypeHClass =
        factory_->NewSEcmaHClass(JSSharedFunction::SIZE, length, JSType::JS_SHARED_FUNCTION,
                                 JSHandle<JSTaggedValue>(sFuncPrototype), JSHandle<JSTaggedValue>(layout));
    sobjPrototypeHClass->SetConstructor(true);
    sobjPrototypeHClass->SetCallable(true);
    return sobjPrototypeHClass;
}

JSHandle<JSHClass> Builtins::CreateSMapFunctionHClass(const JSHandle<JSFunction> &sFuncPrototype) const
{
    uint32_t index = 0;
    auto env = vm_->GetGlobalEnv();
    PropertyAttributes attributes = PropertyAttributes::Default(false, false, false);
    attributes.SetIsInlinedProps(true);
    attributes.SetRepresentation(Representation::TAGGED);
    auto properties = BuiltinsSharedMap::GetFunctionProperties();
    uint32_t length = properties.size();
    JSHandle<JSTaggedValue> keyString;
    JSHandle<LayoutInfo> layout = factory_->CreateSLayoutInfo(length);
    for (const auto &[key, isAccessor] : properties) {
        attributes.SetOffset(index);
        attributes.SetIsAccessor(isAccessor);
        if (key == "[Symbol.species]") {
            keyString = env->GetSpeciesSymbol();
        } else {
            keyString = JSHandle<JSTaggedValue>(factory_->NewFromUtf8(key));
        }
        layout->AddKey(thread_, index++, keyString.GetTaggedValue(), attributes);
    }
    JSHandle<JSHClass> sobjPrototypeHClass =
        factory_->NewSEcmaHClass(JSSharedFunction::SIZE, length, JSType::JS_SHARED_FUNCTION,
                                 JSHandle<JSTaggedValue>(sFuncPrototype), JSHandle<JSTaggedValue>(layout));
    sobjPrototypeHClass->SetConstructor(true);
    sobjPrototypeHClass->SetCallable(true);
    return sobjPrototypeHClass;
}

JSHandle<JSHClass> Builtins::CreateSFunctionPrototypeHClass(const JSHandle<JSTaggedValue> &sObjPrototypeVal) const
{
    uint32_t index = 0;
    auto env = vm_->GetGlobalEnv();
    PropertyAttributes attributes = PropertyAttributes::Default(false, false, false);
    attributes.SetIsInlinedProps(true);
    attributes.SetRepresentation(Representation::TAGGED);
    auto properties = Function::GetFunctionPrototypeProperties();
    uint32_t length = properties.size();
    JSHandle<LayoutInfo> layout = factory_->CreateSLayoutInfo(length);
    JSHandle<JSTaggedValue> keyString;
    for (const std::pair<std::string_view, bool> &each : properties) {
        attributes.SetOffset(index);
        attributes.SetIsAccessor(each.second);
        if (each.first == "[Symbol.hasInstance]") {
            keyString = env->GetHasInstanceSymbol();
        } else {
            keyString = JSHandle<JSTaggedValue>(factory_->NewFromUtf8(each.first));
        }
        layout->AddKey(thread_, index++, keyString.GetTaggedValue(), attributes);
    }
    JSHandle<JSHClass> sobjPrototypeHClass =
        factory_->NewSEcmaHClass(JSSharedFunction::SIZE, length, JSType::JS_SHARED_FUNCTION, sObjPrototypeVal,
                                 JSHandle<JSTaggedValue>(layout));
    sobjPrototypeHClass->SetCallable(true);
    return sobjPrototypeHClass;
}

JSHandle<JSHClass> Builtins::CreateSSetPrototypeHClass(const JSHandle<JSObject> &sObjPrototype) const
{
    uint32_t index = 0;
    auto env = vm_->GetGlobalEnv();
    PropertyAttributes attributes = PropertyAttributes::Default(false, false, false);
    attributes.SetIsInlinedProps(true);
    attributes.SetRepresentation(Representation::TAGGED);
    auto properties = BuiltinsSharedSet::GetPrototypeProperties();
    uint32_t length = properties.size();
    ASSERT(length == BuiltinsSharedSet::GetNumPrototypeInlinedProperties());
    JSHandle<LayoutInfo> layout = factory_->CreateSLayoutInfo(length);
    JSHandle<JSTaggedValue> keyString;
    for (const auto &[key, isAccessor] : properties) {
        attributes.SetOffset(index);
        attributes.SetIsAccessor(isAccessor);
        if (key == "[Symbol.iterator]") {
            keyString = env->GetIteratorSymbol();
        } else if (key == "[Symbol.toStringTag]") {
            keyString = env->GetToStringTagSymbol();
        } else {
            keyString = JSHandle<JSTaggedValue>(factory_->NewFromUtf8(key));
        }
        layout->AddKey(thread_, index++, keyString.GetTaggedValue(), attributes);
    }
    JSHandle<JSHClass> sSetPrototypeHClass =
        factory_->NewSEcmaHClass(JSSharedObject::SIZE, length, JSType::JS_SHARED_OBJECT,
                                 JSHandle<JSTaggedValue>(sObjPrototype),
                                 JSHandle<JSTaggedValue>(layout));
    return sSetPrototypeHClass;
}

JSHandle<JSHClass> Builtins::CreateSMapPrototypeHClass(const JSHandle<JSObject> &sObjPrototype) const
{
    uint32_t index = 0;
    auto env = vm_->GetGlobalEnv();
    PropertyAttributes attributes = PropertyAttributes::Default(false, false, false);
    attributes.SetIsInlinedProps(true);
    attributes.SetRepresentation(Representation::TAGGED);
    auto properties = BuiltinsSharedMap::GetPrototypeProperties();
    uint32_t length = properties.size();
    ASSERT(length == BuiltinsSharedMap::GetNumPrototypeInlinedProperties());
    JSHandle<LayoutInfo> layout = factory_->CreateSLayoutInfo(length);
    JSHandle<JSTaggedValue> keyString;
    for (const auto &[key, isAccessor] : properties) {
        attributes.SetOffset(index);
        attributes.SetIsAccessor(isAccessor);
        if (key == "[Symbol.iterator]") {
            keyString = env->GetIteratorSymbol();
        } else if (key == "[Symbol.toStringTag]") {
            keyString = env->GetToStringTagSymbol();
        } else {
            keyString = JSHandle<JSTaggedValue>(factory_->NewFromUtf8(key));
        }
        layout->AddKey(thread_, index++, keyString.GetTaggedValue(), attributes);
    }
    JSHandle<JSHClass> sMapPrototypeHClass =
        factory_->NewSEcmaHClass(JSSharedObject::SIZE, length, JSType::JS_SHARED_OBJECT,
                                 JSHandle<JSTaggedValue>(sObjPrototype),
                                 JSHandle<JSTaggedValue>(layout));
    return sMapPrototypeHClass;
}

JSHandle<JSHClass> Builtins::CreateSArrayPrototypeHClass(const JSHandle<JSObject> &sObjPrototype) const
{
    uint32_t index = 0;
    auto env = vm_->GetGlobalEnv();
    PropertyAttributes attributes = PropertyAttributes::Default(false, false, false);
    attributes.SetIsInlinedProps(true);
    attributes.SetRepresentation(Representation::TAGGED);
    auto properties = BuiltinsSharedArray::GetPrototypeProperties();
    uint32_t length = properties.size();
    ASSERT(length == BuiltinsSharedArray::GetNumPrototypeInlinedProperties());
    JSHandle<LayoutInfo> layout = factory_->CreateSLayoutInfo(length);
    JSHandle<JSTaggedValue> keyString;
    for (const auto &[key, isAccessor] : properties) {
        attributes.SetOffset(index);
        attributes.SetIsAccessor(isAccessor);
        if (key == "[Symbol.iterator]") {
            keyString = env->GetIteratorSymbol();
        } else if (key == "[Symbol.toStringTag]") {
            keyString = env->GetToStringTagSymbol();
        } else {
            keyString = JSHandle<JSTaggedValue>(factory_->NewFromUtf8(key));
        }
        layout->AddKey(thread_, index++, keyString.GetTaggedValue(), attributes);
    }
    JSHandle<JSHClass> sArrayPrototypeHClass =
        factory_->NewSEcmaHClass(JSSharedArray::SIZE, length, JSType::JS_SHARED_ARRAY,
                                 JSHandle<JSTaggedValue>(sObjPrototype),
                                 JSHandle<JSTaggedValue>(layout));
    return sArrayPrototypeHClass;
}

JSHandle<JSHClass> Builtins::CreateSArrayFunctionHClass(const JSHandle<JSFunction> &sFuncPrototype) const
{
    uint32_t index = 0;
    auto env = vm_->GetGlobalEnv();
    PropertyAttributes attributes = PropertyAttributes::Default(false, false, false);
    attributes.SetIsInlinedProps(true);
    attributes.SetRepresentation(Representation::TAGGED);
    auto properties = BuiltinsSharedArray::GetFunctionProperties();
    uint32_t length = properties.size();
    JSHandle<JSTaggedValue> keyString;
    JSHandle<LayoutInfo> layout = factory_->CreateSLayoutInfo(length);
    for (const auto &[key, isAccessor] : properties) {
        LOG_ECMA(DEBUG) << "CreateSArrayFunctionHClass " << key;
        attributes.SetOffset(index);
        attributes.SetIsAccessor(isAccessor);
        if (key == "[Symbol.species]") {
            keyString = env->GetSpeciesSymbol();
        } else {
            keyString = JSHandle<JSTaggedValue>(factory_->NewFromUtf8(key));
        }
        layout->AddKey(thread_, index++, keyString.GetTaggedValue(), attributes);
    }
    JSHandle<JSHClass> sobjPrototypeHClass =
        factory_->NewSEcmaHClass(JSSharedFunction::SIZE, length, JSType::JS_SHARED_FUNCTION,
                                 JSHandle<JSTaggedValue>(sFuncPrototype), JSHandle<JSTaggedValue>(layout));
    sobjPrototypeHClass->SetConstructor(true);
    sobjPrototypeHClass->SetCallable(true);
    return sobjPrototypeHClass;
}

void Builtins::SetSFunctionName(const JSHandle<JSFunction> &ctor, std::string_view name) const
{
    JSHandle<JSTaggedValue> nameString(factory_->NewFromUtf8(name));
    SetSFunctionName(ctor, nameString);
}

void Builtins::SetSFunctionName(const JSHandle<JSFunction> &ctor, const JSHandle<JSTaggedValue> &name) const
{
    auto nameIndex = JSFunction::NAME_INLINE_PROPERTY_INDEX;
    ctor->SetPropertyInlinedProps(thread_, nameIndex, name.GetTaggedValue());
}

void Builtins::SetSFunctionLength(const JSHandle<JSFunction> &ctor, int length) const
{
    JSTaggedValue taggedLength(length);
    auto lengthIndex = JSFunction::LENGTH_INLINE_PROPERTY_INDEX;
    ctor->SetPropertyInlinedProps(thread_, lengthIndex, taggedLength);
}

void Builtins::SetSFunctionPrototype(const JSHandle<JSFunction> &ctor, const JSTaggedValue &prototype) const
{
    auto prototypeIndex = JSFunction::PROTOTYPE_INLINE_PROPERTY_INDEX;
    ctor->SetPropertyInlinedProps(thread_, prototypeIndex, prototype);
}

void Builtins::InitializeSCtor(const JSHandle<JSHClass> &protoHClass, const JSHandle<JSFunction> &ctor,
                               std::string_view name, int length) const
{
    SetSFunctionLength(ctor, length);
    SetSFunctionName(ctor, name);
    SetSFunctionPrototype(ctor, protoHClass->GetProto());
    ctor->SetProtoOrHClass(thread_, protoHClass);
}

JSHandle<JSFunction> Builtins::NewSFunction(const JSHandle<GlobalEnv> &env, const JSHandle<JSTaggedValue> &key,
                                            EcmaEntrypoint func, int length,
                                            kungfu::BuiltinsStubCSigns::ID builtinId) const
{
    JSHandle<JSHClass> hclass = JSHandle<JSHClass>::Cast(env->GetSFunctionClassWithoutAccessor());
    JSHandle<JSFunction> function = factory_->NewSFunctionByHClass(reinterpret_cast<void *>(func),
        hclass, FunctionKind::NORMAL_FUNCTION, builtinId, MemSpaceType::SHARED_NON_MOVABLE);
    SetSFunctionLength(function, length);
    SetSFunctionName(function, key);
    function->GetJSHClass()->SetExtensible(false);
    return function;
}

void Builtins::SetSFunction(const JSHandle<GlobalEnv> &env, const JSHandle<JSObject> &obj, std::string_view key,
                            EcmaEntrypoint func, uint32_t index, int length,
                            kungfu::BuiltinsStubCSigns::ID builtinId) const
{
    JSHandle<JSTaggedValue> keyString(factory_->NewFromUtf8(key));
    SetSFunction(env, obj, keyString, func, index, length, builtinId);
}

void Builtins::SetSFunction(const JSHandle<GlobalEnv> &env, const JSHandle<JSObject> &obj,
                            const JSHandle<JSTaggedValue> &key, EcmaEntrypoint func, uint32_t index, int length,
                            kungfu::BuiltinsStubCSigns::ID builtinId) const
{
    JSHandle<JSFunction> function(NewSFunction(env, key, func, length, builtinId));
    obj->SetPropertyInlinedProps(thread_, index, function.GetTaggedValue());
}

void Builtins::SetSAccessor(const JSHandle<JSObject> &obj, uint32_t index,
                            const JSHandle<JSTaggedValue> &getter, const JSHandle<JSTaggedValue> &setter) const
{
    JSHandle<AccessorData> accessor = factory_->NewSAccessorData();
    accessor->SetGetter(thread_, getter);
    accessor->SetSetter(thread_, setter);
    obj->SetPropertyInlinedProps(thread_, index, accessor.GetTaggedValue());
}

JSHandle<JSTaggedValue> Builtins::CreateSGetterSetter(const JSHandle<GlobalEnv> &env, EcmaEntrypoint func,
                                                      std::string_view name, int length) const
{
    JSHandle<JSTaggedValue> funcName(factory_->NewFromUtf8(name));
    JSHandle<JSFunction> function = NewSFunction(env, funcName, func, length);
    return JSHandle<JSTaggedValue>(function);
}

void Builtins::SharedStrictModeForbiddenAccessCallerArguments(const JSHandle<GlobalEnv> &env, uint32_t &index,
                                                              const JSHandle<JSObject> &prototype) const
{
    JSHandle<JSHClass> hclass = JSHandle<JSHClass>::Cast(env->GetSFunctionClassWithoutProto());
    JSHandle<JSFunction> func =
        factory_->NewSFunctionWithAccessor(
            reinterpret_cast<void *>(JSFunction::AccessCallerArgumentsThrowTypeError), hclass,
            FunctionKind::NORMAL_FUNCTION);
    // "caller"
    SetSAccessor(prototype, index++, JSHandle<JSTaggedValue>(func), JSHandle<JSTaggedValue>(func));
    // "arguments"
    SetSAccessor(prototype, index++, JSHandle<JSTaggedValue>(func), JSHandle<JSTaggedValue>(func));
}

void Builtins::InitializeSSymbolAttributes(const JSHandle<GlobalEnv> &env)
{
    JSHandle<JSTaggedValue> hasInstanceSymbol(
        factory_->NewSWellKnownSymbolWithChar("Symbol.hasInstance"));
    JSHandle<JSTaggedValue> isConcatSpreadableSymbol(
        factory_->NewSWellKnownSymbolWithChar("Symbol.isConcatSpreadable"));
    JSHandle<JSTaggedValue> toStringTagSymbol(
        factory_->NewSWellKnownSymbolWithChar("Symbol.toStringTag"));
    JSHandle<JSTaggedValue> asyncIteratorSymbol(
        factory_->NewSPublicSymbolWithChar("Symbol.asyncIterator"));
    JSHandle<JSTaggedValue> matchSymbol(
        factory_->NewSPublicSymbolWithChar("Symbol.match"));
    JSHandle<JSTaggedValue> matchAllSymbol(
        factory_->NewSPublicSymbolWithChar("Symbol.matchAll"));
    JSHandle<JSTaggedValue> searchSymbol(
        factory_->NewSPublicSymbolWithChar("Symbol.search"));
    JSHandle<JSTaggedValue> toPrimitiveSymbol(
        factory_->NewSPublicSymbolWithChar("Symbol.toPrimitive"));
    JSHandle<JSTaggedValue> unscopablesSymbol(
        factory_->NewSPublicSymbolWithChar("Symbol.unscopables"));
    JSHandle<JSTaggedValue> nativeBindingSymbol(
        factory_->NewSPublicSymbolWithChar("Symbol.nativeBinding"));

    // Symbol attributes with detectors
    // Create symbol string before create symbol to allocate symbol continuously
    // Attention: Symbol serialization & deserialization are not supported now and
    // the order of symbols and symbol-strings must be maintained too when
    // Symbol serialization & deserialization are ready.
#define INIT_SYMBOL_STRING(name, description, key)                                         \
    {                                                                                      \
        [[maybe_unused]] JSHandle<EcmaString> string = factory_->NewFromUtf8(description); \
    }
DETECTOR_SYMBOL_LIST(INIT_SYMBOL_STRING)
#undef INIT_SYMBOL_STRING

#define INIT_PUBLIC_SYMBOL(name, description, key)                                 \
    JSHandle<JSSymbol> key##Symbol = factory_->NewSEmptySymbol();                  \
    JSHandle<EcmaString> key##String = factory_->NewFromUtf8(description);         \
    key##Symbol->SetDescription(thread_, key##String.GetTaggedValue());            \
    key##Symbol->SetHashField(SymbolTable::Hash(key##String.GetTaggedValue()));
DETECTOR_SYMBOL_LIST(INIT_PUBLIC_SYMBOL)
#undef INIT_PUBLIC_SYMBOL

#define REGISTER_SYMBOL(name, Name)                                                \
    env->Set##Name##Symbol(thread_, name##Symbol);
BUILTIN_ALL_SYMBOLS(REGISTER_SYMBOL)
#undef REGISTER_SYMBOL
}

void Builtins::InitializeSharedArray(const JSHandle<GlobalEnv> &env, const JSHandle<JSObject> &sObjIHClass,
                                     JSHandle<JSFunction> &sFuncPrototype) const
{
    [[maybe_unused]] EcmaHandleScope scope(thread_);
    const GlobalEnvConstants *globalConst = thread_->GlobalConstants();
    // Arraybase.prototype
    JSHandle<JSHClass> arrBaseFuncInstanceHClass = CreateSArrayPrototypeHClass(sObjIHClass);

    // Array.prototype
    JSHandle<JSObject> arrFuncPrototype = factory_->NewSharedOldSpaceJSObjectWithInit(arrBaseFuncInstanceHClass);
    auto accessor = thread_->GlobalConstants()->GetSharedArrayLengthAccessor();
    int32_t protoFieldIndex = JSSharedArray::LENGTH_INLINE_PROPERTY_INDEX;  // length
    static_assert(JSSharedArray::LENGTH_INLINE_PROPERTY_INDEX == 0);
    arrFuncPrototype->SetPropertyInlinedProps(thread_, protoFieldIndex++, accessor);
    JSHandle<JSTaggedValue> arrFuncPrototypeValue(arrFuncPrototype);

    // Array.prototype_or_hclass
    JSHandle<LayoutInfo> layout = factory_->CreateSLayoutInfo(1);
    PropertyAttributes attributes = PropertyAttributes::DefaultAccessor(true, false, false);
    attributes.SetIsInlinedProps(true);
    attributes.SetRepresentation(Representation::TAGGED);
    layout->AddKey(thread_, 0, thread_->GlobalConstants()->GetHandledLengthString().GetTaggedValue(), attributes);
    JSHandle<JSHClass> arrFuncInstanceHClass = factory_->NewSEcmaHClass(
        JSSharedArray::SIZE, 1, JSType::JS_SHARED_ARRAY, arrFuncPrototypeValue, JSHandle<JSTaggedValue>::Cast(layout));
    arrFuncInstanceHClass->SetExtensible(false);
    // SharedArray.hclass
    JSHandle<JSHClass> arrayFuncHClass = CreateSArrayFunctionHClass(sFuncPrototype);
    arrayFuncHClass->SetExtensible(false);
    // SharedArray() = new Function()
    JSHandle<JSFunction> arrayFunction =
        factory_->NewSFunctionByHClass(reinterpret_cast<void *>(BuiltinsSharedArray::ArrayConstructor), arrayFuncHClass,
                                       FunctionKind::BUILTIN_CONSTRUCTOR);

    InitializeSCtor(arrFuncInstanceHClass, arrayFunction, "SharedArray", FunctionLength::ZERO);

    arrFuncPrototype->SetPropertyInlinedProps(thread_, protoFieldIndex++, arrayFunction.GetTaggedValue());
    JSHandle<JSObject> globalObject(thread_, env->GetGlobalObject());
    JSHandle<JSTaggedValue> nameString(factory_->NewFromUtf8("SharedArray"));
    PropertyDescriptor desc(thread_, JSHandle<JSTaggedValue>::Cast(arrayFunction), true, false, true);
    JSObject::DefineOwnProperty(thread_, globalObject, nameString, desc);
    RETURN_IF_ABRUPT_COMPLETION(thread_);
    for (const base::BuiltinFunctionEntry &entry : BuiltinsSharedArray::GetSharedArrayPrototypeFunctions()) {
        SetSFunction(env, arrFuncPrototype, entry.GetName(), entry.GetEntrypoint(), protoFieldIndex++,
                     entry.GetLength(), entry.GetBuiltinStubId());
    }

    // %ArrayPrototype% [ @@iterator ]
    JSHandle<JSTaggedValue> values(factory_->NewFromASCII("values"));
    JSHandle<JSTaggedValue> valuesFunc =
        JSObject::GetMethod(thread_, JSHandle<JSTaggedValue>::Cast(arrFuncPrototype), values);
    RETURN_IF_ABRUPT_COMPLETION(thread_);
    int32_t funcFieldIndex = 3;  // 3: length, name, prototype
    for (const base::BuiltinFunctionEntry &entry : BuiltinsSharedArray::GetSharedArrayFunctions()) {
        SetSFunction(env, JSHandle<JSObject>(arrayFunction), entry.GetName(), entry.GetEntrypoint(), funcFieldIndex++,
                     entry.GetLength(), entry.GetBuiltinStubId());
    }

    // %SetPrototype% [ @@iterator ]
    arrFuncPrototype->SetPropertyInlinedProps(thread_, protoFieldIndex++, valuesFunc.GetTaggedValue());

    // 22.1.2.5 get %Array% [ @@species ]
    JSHandle<JSTaggedValue> speciesGetter =
        CreateSGetterSetter(env, BuiltinsSharedArray::Species, "[Symbol.species]", FunctionLength::ZERO);
    SetSAccessor(JSHandle<JSObject>(arrayFunction), funcFieldIndex++, speciesGetter,
                 globalConst->GetHandledUndefined());

    // Array.prototype [ @@unscopables ]
    JSHandle<JSTaggedValue> unscopablesGetter =
        CreateSGetterSetter(env, BuiltinsSharedArray::Unscopables, "[Symbol.unscopables]", FunctionLength::ZERO);
    SetSAccessor(JSHandle<JSObject>(arrFuncPrototype), protoFieldIndex++, unscopablesGetter,
                 globalConst->GetHandledUndefined());
    arrBaseFuncInstanceHClass->SetExtensible(false);
    env->SetSharedArrayFunction(thread_, arrayFunction);
    env->SetSharedArrayPrototype(thread_, arrFuncPrototype);
}
}  // namespace panda::ecmascript
