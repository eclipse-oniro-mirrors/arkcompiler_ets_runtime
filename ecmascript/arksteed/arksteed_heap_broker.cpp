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

#include "ecmascript/arksteed/arksteed_heap_broker.h"

#include "ecmascript/arksteed/arksteed_feedback_reader.h"
#include "ecmascript/arksteed/arksteed_opcode.h"
#include "ecmascript/js_object-inl.h"
#include "ecmascript/jspandafile/program_object.h"
#include "ecmascript/layout_info.h"
#include "ecmascript/mem/mem.h"

namespace panda::ecmascript::arksteed {

bool ArkSteedHeapBroker::TryGetEmptyArrayLayout(ArkSteedObjectRef* globalEnv, uint32_t* objectSize) const
{
    ASSERT(SerializingAllowed());
    auto env = env_->GetGlobalEnv();
    JSHandle<JSFunction> constructor(env->GetArrayFunctionWithBarrier());
    auto value = constructor->GetProtoOrHClass(compilerThread_);
    // Built-in array classes have fixed slots. Prototype changes replace the class, not its layout.
    *objectSize = JSHClass::Cast(value.GetTaggedObject())->GetObjectSize();
    *globalEnv = MakeObjectRef(env.GetTaggedValue());
    return true;
}

bool ArkSteedHeapBroker::TryGetEmptyObjectLayout(ArkSteedHClassRef* hclass, uint32_t* objectSize) const
{
    ASSERT(SerializingAllowed());
    JSHandle<JSHClass> initial(env_->GetGlobalEnv()->GetObjectFunctionClassWithBarrier());
    *objectSize = initial->GetObjectSize();
    *hclass = MakeHClassRef(initial.GetTaggedValue());
    return true;
}

bool ArkSteedHeapBroker::TryGetArrayLiteralFromConstantPool(uint16_t cpIdx,
                                                            ArkSteedObjectRef* literal,
                                                            uint32_t* objectSize) const
{
    ASSERT(literal != nullptr);
    ASSERT(SerializingAllowed());
    if (env_->GetMethodLiteral() == nullptr) {
        return false;
    }
    uint32_t methodOffset = env_->GetMethodLiteral()->GetMethodId().GetOffset();
    JSTaggedValue constpool = env_->FindOrCreateUnsharedConstpool(methodOffset);
    if (!constpool.IsConstantPool()) {
        return false;
    }
    auto* pool = ConstantPool::Cast(constpool.GetTaggedObject());
    // AOT literals may select a different HClass through the constant-index mapping.
    if (pool->GetJSPandaFile()->IsLoadedAOT() || cpIdx >= pool->GetNumOfCacheElement()) {
        return false;
    }
    // JIT must not lazily materialize a literal on the compiler thread.
    JSTaggedValue value = env_->GetArrayLiteralFromCache(constpool, cpIdx, CString());
    if (!value.IsJSArray()) {
        return false;
    }
    auto* array = JSArray::Cast(value.GetTaggedObject());
    auto elements = array->GetElements(compilerThread_);
    auto empty = compilerThread_->GlobalConstants()->GetEmptyArray();
    // Materialization uses the built-in array layout, empty properties and local elements.
    // CloneArrayLiteral may install COW once, but never mutates an already-COW template store.
    if (elements != empty && !elements.IsCOWArray()) {
        return false;
    }
    *objectSize = array->GetJSHClass()->GetObjectSize();
    *literal = MakeObjectRef(value);
    return true;
}

bool ArkSteedHeapBroker::TryGetObjectLiteralFromConstantPool(uint16_t cpIdx,
                                                             Chunk* chunk,
                                                             ArkSteedObjectRef* literal,
                                                             CreateObjectWithBufferInfo* info) const
{
    ASSERT(chunk != nullptr && literal != nullptr && info != nullptr);
    ASSERT(SerializingAllowed());
    ASSERT(env_->GetMethodLiteral() != nullptr);
    uint32_t methodOffset = env_->GetMethodLiteral()->GetMethodId().GetOffset();
    auto poolValue = env_->NewJSHandle(env_->FindOrCreateUnsharedConstpool(methodOffset));
    if (!poolValue.GetTaggedValue().IsConstantPool()) {
        return false;
    }
    auto pool = JSHandle<ConstantPool>::Cast(poolValue);
    if (pool->GetJSPandaFile()->IsLoadedAOT() || cpIdx >= pool->GetNumOfCacheElement()) {
        return false;
    }
    // This JIT accessor only reads the cache; it never materializes a cold literal.
    auto value = env_->NewJSHandle(env_->GetObjectLiteralFromCache(pool.GetTaggedValue(), cpIdx, CString()));
    if (!value.GetTaggedValue().IsJSObject()) {
        return false;
    }
    auto object = JSHandle<JSObject>::Cast(value);
    auto hclass = JSHandle<JSHClass>::Cast(env_->NewJSHandle(JSTaggedValue(object->GetClass())));
    // The global literal class cache can contain AOT classes even for a non-AOT file.
    // Their spare slots need Hole initialization; non-empty backing stores need cloning.
    if (hclass->IsAOT() || !object->ElementsAndPropertiesIsEmpty(compilerThread_)
        || hclass->GetObjectSize() > g_maxRegularHeapObjectSize) {
        return false;
    }
    CreateObjectWithBufferInfo snapshot;
    snapshot.objectSize = hclass->GetObjectSize();
    snapshot.fieldCount = hclass->NumberOfProps();
    // Cached ordinary literals are private templates. Clone writes target the new instance;
    // representation transitions copy the layout and detach the new transition ancestry.
    auto layout = JSHandle<LayoutInfo>::Cast(env_->NewJSHandle(hclass->GetLayout(compilerThread_)));
    auto* fieldValues = snapshot.fieldCount == 0 ? nullptr : chunk->NewArray<uint64_t>(snapshot.fieldCount);
    for (uint32_t i = 0; i < snapshot.fieldCount; ++i) {
        PropertyAttributes attr = layout->GetAttr(compilerThread_, i);
        JSTaggedValue field = object->GetPropertyInlinedProps(compilerThread_, i);
        // Raw INT/DOUBLE slots already contain converted bits, not tagged values. Functions, accessors and
        // other heap-valued tagged slots need the general clone/barrier path and cannot be embedded here.
        if (attr.IsTaggedRep() && field.IsHeapObject()) {
            return false;
        }
        fieldValues[i] = field.GetRawData();
    }
    snapshot.fieldValues = Span<const uint64_t>(fieldValues, snapshot.fieldCount);
    *literal = MakeObjectRef(object.GetTaggedValue());
    *info = snapshot;
    return true;
}

bool ArkSteedHeapBroker::GetFeedbackForNamedAccess(const ArkSteedFeedbackReader &reader, int slotIndex,
                                                   NamedAccessFeedback *feedback) const
{
    *feedback = {};
    uint32_t slotId = 0;
    if (!reader.TryGetFeedbackSlotId(slotIndex, false, &slotId)) {
        return false;
    }
    if (TryGetCachedNamedAccessFeedback({slotId, false}, feedback) ||
        TryGetCachedNamedAccessFeedback({slotId, true}, feedback)) {
        return true;
    }

    SerializingScope scope(this, "ArkSteedHeapBroker::GetFeedbackForNamedAccess");
    bool success = reader.ReadNamedAccessFeedback(slotIndex, feedback);
    if (!success) {
        *feedback = {};
        return false;
    }
    CacheNamedAccessFeedback(*feedback);
    return true;
}

bool ArkSteedHeapBroker::GetFeedbackForOperation(const ArkSteedFeedbackReader &reader,
                                                 OperationFeedback *feedback) const
{
    *feedback = {};
    SerializingScope scope(this, "ArkSteedHeapBroker::GetFeedbackForOperation");
    return reader.ReadOperationFeedback(feedback);
}

bool ArkSteedHeapBroker::GetFeedbackForValueAccess(const ArkSteedFeedbackReader &reader,
                                                   ValueAccessFeedback *feedback) const
{
    *feedback = {};
    uint32_t slotId = 0;
    if (!reader.TryGetFeedbackSlotId(&slotId)) {
        return false;
    }
    if (TryGetCachedValueAccessFeedback(slotId, feedback)) {
        return true;
    }

    SerializingScope scope(this, "ArkSteedHeapBroker::GetFeedbackForValueAccess");
    if (!reader.ReadValueAccessFeedback(feedback)) {
        *feedback = {};
        return false;
    }
    CacheValueAccessFeedback(*feedback);
    return true;
}

bool ArkSteedHeapBroker::GetFeedbackForElementAccess(const ArkSteedFeedbackReader &reader, int slotIndex,
                                                     ElementAccessFeedback *feedback) const
{
    *feedback = {};
    SerializingScope scope(this, "ArkSteedHeapBroker::GetFeedbackForElementAccess");
    return reader.ReadElementAccessFeedback(slotIndex, feedback);
}

bool ArkSteedHeapBroker::GetFeedbackForGlobalAccess(const ArkSteedFeedbackReader &reader,
                                                    GlobalAccessFeedback *feedback) const
{
    *feedback = {};
    SerializingScope scope(this, "ArkSteedHeapBroker::GetFeedbackForGlobalAccess");
    return reader.ReadGlobalAccessFeedback(feedback);
}

bool ArkSteedHeapBroker::GetFeedbackForCall(const ArkSteedFeedbackReader &reader, CallFeedback *feedback) const
{
    *feedback = {};
    SerializingScope scope(this, "ArkSteedHeapBroker::GetFeedbackForCall");
    return reader.ReadCallFeedback(feedback);
}

}  // namespace panda::ecmascript::arksteed
