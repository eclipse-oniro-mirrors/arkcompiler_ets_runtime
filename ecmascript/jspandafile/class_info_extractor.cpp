/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
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

#include "ecmascript/jspandafile/class_info_extractor.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_function.h"
#include "ecmascript/jspandafile/program_object.h"
#include "ecmascript/jspandafile/method_literal.h"
#include "ecmascript/tagged_dictionary.h"

namespace panda::ecmascript {
void ClassInfoExtractor::BuildClassInfoExtractorFromLiteral(JSThread *thread, JSHandle<ClassInfoExtractor> &extractor,
                                                            const JSHandle<TaggedArray> &literal)
{
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();

    uint32_t literalBufferLength = literal->GetLength();
    // non static properties number is hidden in the last index of Literal buffer
    uint32_t nonStaticNum = 0;
    if (literalBufferLength != 0) {
        nonStaticNum = static_cast<uint32_t>(literal->Get(thread, literalBufferLength - 1).GetInt());
    }

    // Reserve sufficient length to prevent frequent creation.
    JSHandle<TaggedArray> nonStaticKeys = factory->NewOldSpaceTaggedArray(nonStaticNum + NON_STATIC_RESERVED_LENGTH);
    JSHandle<TaggedArray> nonStaticProperties =
        factory->NewOldSpaceTaggedArray(nonStaticNum + NON_STATIC_RESERVED_LENGTH);

    nonStaticKeys->Set(thread, CONSTRUCTOR_INDEX, globalConst->GetConstructorString());

    if (nonStaticNum) {
        ExtractContentsDetail nonStaticDetail {0, nonStaticNum * 2, NON_STATIC_RESERVED_LENGTH, nullptr};

        JSHandle<TaggedArray> nonStaticElements = factory->EmptyArray();
        if (UNLIKELY(ExtractAndReturnWhetherWithElements(thread, literal, nonStaticDetail, nonStaticKeys,
                                                         nonStaticProperties, nonStaticElements))) {
            extractor->SetNonStaticWithElements(true);
            extractor->SetNonStaticElements(thread, nonStaticElements);
        }
    }

    extractor->SetNonStaticKeys(thread, nonStaticKeys);
    extractor->SetNonStaticProperties(thread, nonStaticProperties);

    uint32_t staticNum = literalBufferLength == 0 ? 0 : (literalBufferLength - 1) / 2 - nonStaticNum;

    // Reserve sufficient length to prevent frequent creation.
    JSHandle<TaggedArray> staticKeys = factory->NewOldSpaceTaggedArray(staticNum + STATIC_RESERVED_LENGTH);
    JSHandle<TaggedArray> staticProperties = factory->NewOldSpaceTaggedArray(staticNum + STATIC_RESERVED_LENGTH);

    staticKeys->Set(thread, LENGTH_INDEX, globalConst->GetLengthString());
    staticKeys->Set(thread, NAME_INDEX, globalConst->GetNameString());
    staticKeys->Set(thread, PROTOTYPE_INDEX, globalConst->GetPrototypeString());

    JSHandle<TaggedArray> staticElements = factory->EmptyArray();

    if (staticNum) {
        ExtractContentsDetail staticDetail {
            nonStaticNum * 2,
            literalBufferLength - 1,
            STATIC_RESERVED_LENGTH,
            Method::Cast(extractor->GetConstructorMethod().GetTaggedObject())
        };

        if (UNLIKELY(ExtractAndReturnWhetherWithElements(thread, literal, staticDetail, staticKeys,
                                                         staticProperties, staticElements))) {
            extractor->SetStaticWithElements(true);
            extractor->SetStaticElements(thread, staticElements);
        }
    } else {
        // without static properties, set class name
        std::string clsName = Method::Cast(extractor->GetConstructorMethod().GetTaggedObject())->ParseFunctionName();
        JSHandle<EcmaString> clsNameHandle = factory->NewFromStdString(clsName);
        staticProperties->Set(thread, NAME_INDEX, clsNameHandle);
    }

    // set prototype internal accessor
    JSHandle<JSTaggedValue> prototypeAccessor = globalConst->GetHandledFunctionPrototypeAccessor();
    staticProperties->Set(thread, PROTOTYPE_INDEX, prototypeAccessor);

    extractor->SetStaticKeys(thread, staticKeys);
    extractor->SetStaticProperties(thread, staticProperties);
}

bool ClassInfoExtractor::ExtractAndReturnWhetherWithElements(JSThread *thread, const JSHandle<TaggedArray> &literal,
                                                             const ExtractContentsDetail &detail,
                                                             JSHandle<TaggedArray> &keys,
                                                             JSHandle<TaggedArray> &properties,
                                                             JSHandle<TaggedArray> &elements)
{
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();

    ASSERT(keys->GetLength() == properties->GetLength() && elements->GetLength() == 0);

    uint32_t pos = detail.fillStartLoc;
    bool withElementsFlag = false;
    bool isStaticFlag = detail.method ? true : false;
    bool keysHasNameFlag = false;

    JSHandle<JSTaggedValue> nameString = globalConst->GetHandledNameString();
    JSMutableHandle<JSTaggedValue> firstValue(thread, JSTaggedValue::Undefined());
    JSMutableHandle<JSTaggedValue> secondValue(thread, JSTaggedValue::Undefined());
    for (uint32_t index = detail.extractBegin; index < detail.extractEnd; index += 2) {  // 2: key-value pair
        firstValue.Update(literal->Get(index));
        secondValue.Update(literal->Get(index + 1));
        ASSERT_PRINT(JSTaggedValue::IsPropertyKey(firstValue), "Key is not a property key");

        if (LIKELY(firstValue->IsString())) {
            if (isStaticFlag && !keysHasNameFlag && JSTaggedValue::SameValue(firstValue, nameString)) {
                properties->Set(thread, NAME_INDEX, secondValue);
                keysHasNameFlag = true;
                continue;
            }

            // front-end can do better: write index in class literal directly.
            uint32_t elementIndex = 0;
            if (JSTaggedValue::StringToElementIndex(firstValue.GetTaggedValue(), &elementIndex)) {
                ASSERT(elementIndex < JSObject::MAX_ELEMENT_INDEX);
                uint32_t elementsLength = elements->GetLength();
                elements =
                    TaggedArray::SetCapacityInOldSpace(thread, elements, elementsLength + 2); // 2: key-value pair
                elements->Set(thread, elementsLength, firstValue);
                elements->Set(thread, elementsLength + 1, secondValue);
                withElementsFlag = true;
                continue;
            }
        }

        keys->Set(thread, pos, firstValue);
        properties->Set(thread, pos, secondValue);
        pos++;
    }

    if (isStaticFlag) {
        if (LIKELY(!keysHasNameFlag)) {
            [[maybe_unused]] EcmaHandleScope handleScope(thread);
            ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
            std::string clsName = detail.method->ParseFunctionName();
            JSHandle<EcmaString> clsNameHandle = factory->NewFromStdString(clsName);
            properties->Set(thread, NAME_INDEX, clsNameHandle);
        } else {
            // class has static name property, reserved length bigger 1 than actual, need trim
            uint32_t trimOneLength = keys->GetLength() - 1;
            keys->Trim(thread, trimOneLength);
            properties->Trim(thread, trimOneLength);
        }
    }

    if (UNLIKELY(withElementsFlag)) {
        ASSERT(pos + elements->GetLength() / 2 == properties->GetLength());  // 2: half
        keys->Trim(thread, pos);
        properties->Trim(thread, pos);
    }

    return withElementsFlag;
}

JSHandle<JSHClass> ClassInfoExtractor::CreatePrototypeHClass(JSThread *thread, JSHandle<TaggedArray> &keys,
                                                             JSHandle<TaggedArray> &properties)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();

    uint32_t length = keys->GetLength();
    JSHandle<JSHClass> hclass;
    if (LIKELY(length <= PropertyAttributes::MAX_CAPACITY_OF_PROPERTIES)) {
        JSMutableHandle<JSTaggedValue> key(thread, JSTaggedValue::Undefined());
        JSHandle<LayoutInfo> layout = factory->CreateLayoutInfo(length, MemSpaceType::OLD_SPACE, GrowMode::KEEP);
        for (uint32_t index = 0; index < length; ++index) {
            key.Update(keys->Get(index));
            ASSERT_PRINT(JSTaggedValue::IsPropertyKey(key), "Key is not a property key");
            PropertyAttributes attributes = PropertyAttributes::Default(true, false, true);  // non-enumerable

            if (UNLIKELY(properties->Get(index).IsAccessor())) {
                attributes.SetIsAccessor(true);
            }

            attributes.SetIsInlinedProps(true);
            attributes.SetRepresentation(Representation::MIXED);
            attributes.SetOffset(index);
            layout->AddKey(thread, index, key.GetTaggedValue(), attributes);
        }

        hclass = factory->NewEcmaHClass(JSObject::SIZE, JSType::JS_OBJECT, length);
        // Not need set proto here
        hclass->SetLayout(thread, layout);
        hclass->SetNumberOfProps(length);
    } else {
        // dictionary mode
        hclass = factory->NewEcmaHClass(JSObject::SIZE, JSType::JS_OBJECT, 0);  // without in-obj
        hclass->SetIsDictionaryMode(true);
        hclass->SetNumberOfProps(0);
    }

    hclass->SetClassPrototype(true);
    hclass->SetIsPrototype(true);
    return hclass;
}

JSHandle<JSHClass> ClassInfoExtractor::CreateConstructorHClass(JSThread *thread, JSHandle<TaggedArray> &keys,
                                                               JSHandle<TaggedArray> &properties)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();

    uint32_t length = keys->GetLength();
    JSHandle<JSHClass> hclass;
    if (LIKELY(length <= PropertyAttributes::MAX_CAPACITY_OF_PROPERTIES)) {
        JSMutableHandle<JSTaggedValue> key(thread, JSTaggedValue::Undefined());
        JSHandle<LayoutInfo> layout = factory->CreateLayoutInfo(length, MemSpaceType::OLD_SPACE, GrowMode::KEEP);
        for (uint32_t index = 0; index < length; ++index) {
            key.Update(keys->Get(index));
            ASSERT_PRINT(JSTaggedValue::IsPropertyKey(key), "Key is not a property key");
            PropertyAttributes attributes;
            switch (index) {
                case LENGTH_INDEX:
                    attributes = PropertyAttributes::Default(false, false, true);
                    break;
                case NAME_INDEX:
                    if (LIKELY(properties->Get(NAME_INDEX).IsString())) {
                        attributes = PropertyAttributes::Default(false, false, true);
                    } else {
                        ASSERT(properties->Get(NAME_INDEX).IsJSFunction());
                        attributes = PropertyAttributes::Default(true, false, true);
                    }
                    break;
                case PROTOTYPE_INDEX:
                    attributes = PropertyAttributes::DefaultAccessor(false, false, false);
                    break;
                default:
                    attributes = PropertyAttributes::Default(true, false, true);
                    break;
            }

            if (UNLIKELY(properties->Get(index).IsAccessor())) {
                attributes.SetIsAccessor(true);
            }

            attributes.SetIsInlinedProps(true);
            attributes.SetRepresentation(Representation::MIXED);
            attributes.SetOffset(index);
            layout->AddKey(thread, index, key.GetTaggedValue(), attributes);
        }

        hclass = factory->NewEcmaHClass(JSFunction::SIZE, JSType::JS_FUNCTION, length);
        // Not need set proto here
        hclass->SetLayout(thread, layout);
        hclass->SetNumberOfProps(length);
    } else {
        // dictionary mode
        hclass = factory->NewEcmaHClass(JSFunction::SIZE, JSType::JS_FUNCTION, 0);  // without in-obj
        hclass->SetIsDictionaryMode(true);
        hclass->SetNumberOfProps(0);
    }

    hclass->SetClassConstructor(true);
    hclass->SetConstructor(true);

    return hclass;
}

JSHandle<JSFunction> ClassHelper::DefineClassFromExtractor(JSThread *thread, JSHandle<ClassInfoExtractor> &extractor,
                                                           const JSHandle<JSTaggedValue> &constpool,
                                                           const JSHandle<JSTaggedValue> &lexenv)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    JSHandle<ConstantPool> constantPool = JSHandle<ConstantPool>::Cast(constpool);
    JSHandle<TaggedArray> staticKeys(thread, extractor->GetStaticKeys());
    JSHandle<TaggedArray> staticProperties(thread, extractor->GetStaticProperties());
    JSHandle<JSHClass> constructorHClass = ClassInfoExtractor::CreateConstructorHClass(thread, staticKeys,
                                                                                       staticProperties);

    JSHandle<TaggedArray> nonStaticKeys(thread, extractor->GetNonStaticKeys());
    JSHandle<TaggedArray> nonStaticProperties(thread, extractor->GetNonStaticProperties());
    JSHandle<JSHClass> prototypeHClass = ClassInfoExtractor::CreatePrototypeHClass(thread, nonStaticKeys,
                                                                                   nonStaticProperties);

    JSHandle<JSObject> prototype = factory->NewOldSpaceJSObject(prototypeHClass);

    JSHandle<Method> method(thread, Method::Cast(extractor->GetConstructorMethod().GetTaggedObject()));
    JSHandle<JSFunction> constructor = factory->NewJSFunctionByHClass(method,
        constructorHClass, MemSpaceType::OLD_SPACE);

    // non-static
    nonStaticProperties->Set(thread, 0, constructor);

    uint32_t nonStaticLength = nonStaticProperties->GetLength();
    JSMutableHandle<JSTaggedValue> propValue(thread, JSTaggedValue::Undefined());

    if (LIKELY(!prototypeHClass->IsDictionaryMode())) {
        for (uint32_t index = 0; index < nonStaticLength; ++index) {
            propValue.Update(nonStaticProperties->Get(index));
            if (propValue->IsJSFunction()) {
                JSHandle<JSFunction> propFunc = factory->CloneJSFuction(JSHandle<JSFunction>::Cast(propValue));
                propFunc->SetHomeObject(thread, prototype);
                JSHandle<Method> propMethod(thread, propFunc->GetMethod());
                propMethod->SetConstantPool(thread, constantPool.GetTaggedValue());
                propFunc->SetLexicalEnv(thread, lexenv);
                propValue.Update(propFunc);
            }
            prototype->SetPropertyInlinedProps(thread, index, propValue.GetTaggedValue());
        }
    } else {
        JSHandle<NameDictionary> dict = BuildDictionaryProperties(thread, prototype, nonStaticKeys, nonStaticProperties,
                                                                  ClassPropertyType::NON_STATIC, constantPool, lexenv);
        prototype->SetProperties(thread, dict);
    }

    // non-static elements
    if (UNLIKELY(extractor->GetNonStaticWithElements())) {
        JSHandle<TaggedArray> nonStaticElements(thread, extractor->GetNonStaticElements());
        ClassHelper::HandleElementsProperties(thread, prototype, nonStaticElements, constantPool);
    }

    // static
    uint32_t staticLength = staticProperties->GetLength();

    if (LIKELY(!constructorHClass->IsDictionaryMode())) {
        for (uint32_t index = 0; index < staticLength; ++index) {
            propValue.Update(staticProperties->Get(index));
            if (propValue->IsJSFunction()) {
                JSHandle<JSFunction> propFunc = factory->CloneJSFuction(JSHandle<JSFunction>::Cast(propValue));
                propFunc->SetHomeObject(thread, constructor);
                JSHandle<Method> propMethod(thread, propFunc->GetMethod());
                propMethod->SetConstantPool(thread, constantPool.GetTaggedValue());
                propFunc->SetLexicalEnv(thread, lexenv);
                propValue.Update(propFunc);
            }
            JSHandle<JSObject>::Cast(constructor)->SetPropertyInlinedProps(thread, index, propValue.GetTaggedValue());
        }
    } else {
        JSHandle<NameDictionary> dict = BuildDictionaryProperties(thread, JSHandle<JSObject>(constructor), staticKeys,
                                                                  staticProperties, ClassPropertyType::STATIC,
                                                                  constantPool, lexenv);
        constructor->SetProperties(thread, dict);
    }

    // static elements
    if (UNLIKELY(extractor->GetStaticWithElements())) {
        JSHandle<TaggedArray> staticElements(thread, extractor->GetStaticElements());
        ClassHelper::HandleElementsProperties(thread, JSHandle<JSObject>(constructor), staticElements, constantPool);
    }

    PropertyDescriptor ctorDesc(thread, JSHandle<JSTaggedValue>(constructor), true, false, true);
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    JSTaggedValue::DefinePropertyOrThrow(thread, JSHandle<JSTaggedValue>(prototype),
                                         globalConst->GetHandledConstructorString(), ctorDesc);

    constructor->SetHomeObject(thread, prototype);
    constructor->SetProtoOrHClass(thread, prototype);

    return constructor;
}

JSHandle<NameDictionary> ClassHelper::BuildDictionaryProperties(JSThread *thread, const JSHandle<JSObject> &object,
                                                                JSHandle<TaggedArray> &keys,
                                                                JSHandle<TaggedArray> &properties,
                                                                ClassPropertyType type,
                                                                const JSHandle<ConstantPool> &constantpool,
                                                                const JSHandle<JSTaggedValue> &lexenv)
{
    ObjectFactory *factory = thread->GetEcmaVM()->GetFactory();
    uint32_t length = keys->GetLength();
    ASSERT(length > PropertyAttributes::MAX_CAPACITY_OF_PROPERTIES);
    ASSERT(keys->GetLength() == properties->GetLength());

    JSMutableHandle<NameDictionary> dict(
        thread, NameDictionary::Create(thread, NameDictionary::ComputeHashTableSize(length)));
    JSMutableHandle<JSTaggedValue> propKey(thread, JSTaggedValue::Undefined());
    JSMutableHandle<JSTaggedValue> propValue(thread, JSTaggedValue::Undefined());
    for (uint32_t index = 0; index < length; index++) {
        PropertyAttributes attributes;
        if (type == ClassPropertyType::STATIC) {
            switch (index) {
                case ClassInfoExtractor::LENGTH_INDEX:
                    attributes = PropertyAttributes::Default(false, false, true);
                    break;
                case ClassInfoExtractor::NAME_INDEX:
                    if (LIKELY(properties->Get(ClassInfoExtractor::NAME_INDEX).IsString())) {
                        attributes = PropertyAttributes::Default(false, false, true);
                    } else {
                        ASSERT(properties->Get(ClassInfoExtractor::NAME_INDEX).IsJSFunction());
                        attributes = PropertyAttributes::Default(true, false, true);
                    }
                    break;
                case ClassInfoExtractor::PROTOTYPE_INDEX:
                    attributes = PropertyAttributes::DefaultAccessor(false, false, false);
                    break;
                default:
                    attributes = PropertyAttributes::Default(true, false, true);
                    break;
            }
        } else {
            attributes = PropertyAttributes::Default(true, false, true);  // non-enumerable
        }
        propKey.Update(keys->Get(index));
        propValue.Update(properties->Get(index));
        if (propValue->IsJSFunction()) {
            JSHandle<JSFunction> propFunc = factory->CloneJSFuction(JSHandle<JSFunction>::Cast(propValue));
            propFunc->SetHomeObject(thread, object);
            propFunc->SetLexicalEnv(thread, lexenv);
            JSHandle<Method> method(thread, propFunc->GetMethod());
            method->SetConstantPool(thread, constantpool.GetTaggedValue());
            propValue.Update(propFunc);
        }
        JSHandle<NameDictionary> newDict = NameDictionary::PutIfAbsent(thread, dict, propKey, propValue, attributes);
        dict.Update(newDict);
    }
    return dict;
}

void ClassHelper::HandleElementsProperties(JSThread *thread, const JSHandle<JSObject> &object,
                                           JSHandle<TaggedArray> &elements, const JSHandle<ConstantPool> &constantpool)
{
    JSMutableHandle<JSTaggedValue> elementsKey(thread, JSTaggedValue::Undefined());
    JSMutableHandle<JSTaggedValue> elementsValue(thread, JSTaggedValue::Undefined());
    for (uint32_t index = 0; index < elements->GetLength(); index += 2) {  // 2: key-value pair
        elementsKey.Update(elements->Get(index));
        elementsValue.Update(elements->Get(index + 1));
        // class property attribute is not default, will transition to dictionary directly.
        JSObject::DefinePropertyByLiteral(thread, object, elementsKey, elementsValue, true);

        if (elementsValue->IsJSFunction()) {
            JSHandle<JSFunction> elementsFunc = JSHandle<JSFunction>::Cast(elementsValue);
            elementsFunc->SetHomeObject(thread, object);
            JSHandle<Method> method(thread, elementsFunc->GetMethod());
            method->SetConstantPool(thread, constantpool.GetTaggedValue());
        }
    }
}
}  // namespace panda::ecmascript
