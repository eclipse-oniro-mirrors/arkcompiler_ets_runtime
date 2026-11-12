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

#ifndef ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_MARK_VISITOR_H
#define ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_MARK_VISITOR_H

#include "ecmascript/mem/global_cc/global_mark_work_manager.h"
#include "ecmascript/mem/visitor.h"

namespace panda::ecmascript {
class GlobalCCMarkRootVisitor final : public RootVisitor {
public:
    inline explicit GlobalCCMarkRootVisitor(GlobalCCWorkNodeHolder *holder) : holder_(holder) {}
    ~GlobalCCMarkRootVisitor() override = default;

    inline void VisitRoot([[maybe_unused]] Root type, ObjectSlot slot) override;

    inline void VisitRangeRoot([[maybe_unused]] Root type, ObjectSlot start, ObjectSlot end) override;

    inline void VisitBaseAndDerivedRoot([[maybe_unused]] Root type, [[maybe_unused]] ObjectSlot base,
                                        [[maybe_unused]] ObjectSlot derived,
                                        [[maybe_unused]] uintptr_t baseOldObject) override {};

private:
    inline void HandleSlot(ObjectSlot slot);

    GlobalCCWorkNodeHolder *holder_ {nullptr};
};

class GlobalCCMarkObjectVisitor final : public BaseObjectVisitor<GlobalCCMarkObjectVisitor> {
public:
    inline explicit GlobalCCMarkObjectVisitor(GlobalCCWorkNodeHolder *holder) : holder_(holder) {}
    ~GlobalCCMarkObjectVisitor() override = default;

    inline void VisitObjectRangeImpl(BaseObject *rootObject, ObjectSlot start, ObjectSlot end,
                                     VisitObjectArea area) override;

    inline void VisitCompressedObjectRangeImpl(BaseObject *rootObject, CompressedObjectSlot start,
                                               CompressedObjectSlot end) override;

    inline void VisitObjectHClassImpl(BaseObject *rootObject, BaseObject *hclass) override;

    inline void VisitWeakLinkedHashMapImpl(BaseObject *rootObject) override;

    inline void HandleObject(TaggedObject *object);

private:
    template <ReferenceType refType>
    inline void HandleSlot(ObjectSlotBase<refType> slot);
    inline bool IsAlive(TaggedObject *obj);
    inline void RecordWeakReference(JSTaggedType *weak);

    GlobalCCWorkNodeHolder *holder_ {nullptr};
};
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_MEM_GLOBAL_CC_GLOBAL_MARK_VISITOR_H
