/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_JSNATIVEPOINTER_H
#define ECMASCRIPT_JSNATIVEPOINTER_H

#include "ecmascript/ecma_macros.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/mem/tagged_object.h"
#include "ecmascript/mem/native_area_allocator.h"

namespace panda::ecmascript {
// Used for the requirement of ACE that wants to associated a registered C++ resource with a JSObject.
class JSNativePointer : public TaggedObject {
public:
    static JSNativePointer *Cast(TaggedObject *object)
    {
        ASSERT(JSTaggedValue(object).IsJSNativePointer());
        return reinterpret_cast<JSNativePointer *>(object);
    }

    inline void ResetExternalPointer(JSThread *thread, void *externalPointer)
    {
        DeleteExternalPointer(thread);
        SetExternalPointer(externalPointer);
    }

    inline void Destroy(JSThread *thread)
    {
        DeleteExternalPointer(thread);
        SetExternalPointer(nullptr);
        SetDeleter(nullptr);
        SetData(nullptr);
        SetNativeFlag(NativeFlag::NO_DIV);
    }

    inline void Detach()
    {
        // Keep other fields accessible after detached
        SetDeleter(nullptr);
    }

    static constexpr size_t POINTER_OFFSET = TaggedObjectSize();
    ACCESSORS_NATIVE_FIELD(ExternalPointer, void, POINTER_OFFSET, DELETER_OFFSET);
    ACCESSORS_PRIMITIVE_FIELD(Deleter, NativePointerCallback, DELETER_OFFSET, DATA_OFFSET)
    ACCESSORS_NATIVE_FIELD(Data, void, DATA_OFFSET, DATA_SIZE_OFFSET);
    ACCESSORS_PRIMITIVE_FIELD(BindingSize, uint64_t, DATA_SIZE_OFFSET, FLAG_OFFSET);
    // native memory statistic flag
    ACCESSORS_PRIMITIVE_FIELD(NativeFlag, NativeFlag, FLAG_OFFSET, LAST_OFFSET);
    DEFINE_ALIGN_SIZE(LAST_OFFSET);

    DECL_VISIT_NATIVE_FIELD(POINTER_OFFSET, DATA_SIZE_OFFSET)

private:
    inline void DeleteExternalPointer(JSThread *thread)
    {
        void *externalPointer = GetExternalPointer();
        NativePointerCallback deleter = GetDeleter();
        auto env = thread->GetEnv();
        if (deleter != nullptr) {
            deleter(env, externalPointer, GetData());
        }
    }
};
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_JSNATIVEPOINTER_H
