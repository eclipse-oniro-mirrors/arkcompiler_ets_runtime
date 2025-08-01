/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_NAPI_INCLUDE_JSNAPI_CLASS_FACTORY_H
#define ECMASCRIPT_NAPI_INCLUDE_JSNAPI_CLASS_FACTORY_H

#include <cstdint>

namespace panda {
class PropertyAttribute;
template <typename T> class Local;
class JSValueRef;
class JsiRuntimeCallInfo;
namespace ecmascript {
class JSThread;
class PropertyDescriptor;
template <typename T> class JSHandle;
class JSTaggedValue;
class JSHClass;
class JSObject;
class JSFunction;

using std::size_t;
using InternalFunctionCallback = JSValueRef(*)(JsiRuntimeCallInfo*);

class JSNApiClassCreationHelper {
public:
    static JSHandle<JSFunction> CreateClassFuncWithProperties(JSThread *thread, const char *name,
                                                              InternalFunctionCallback nativeFunc, bool callNapi,
                                                              size_t propertyCount, size_t staticPropCount,
                                                              const Local<JSValueRef> *keys, PropertyAttribute *attrs,
                                                              PropertyDescriptor *descs);
private:
    static inline void ConstructDescByAttr(const JSThread *thread, const PropertyAttribute &attr,
                                           PropertyDescriptor *desc);
    static inline void DestructDesc(PropertyDescriptor *desc);
    static inline void DestructAttr(PropertyAttribute *attr);
    static bool TryAddOriKeyAndOriAttrToHClass(const JSThread *thread, const Local<JSValueRef> &oriKey,
                                               const PropertyAttribute &oriAttr, PropertyDescriptor &desc,
                                               size_t &attrOffset, JSHandle<JSHClass> &hClass);
    static JSHandle<JSObject> NewClassFuncProtoWithProperties(JSThread *thread, const JSHandle<JSFunction> &func,
                                                              size_t propertyCount, size_t nonStaticPropCount,
                                                              const Local<JSValueRef> *keys, PropertyAttribute *attrs,
                                                              PropertyDescriptor *descs);
};
} // namespace ecmacript
} // namespace panda

#endif
