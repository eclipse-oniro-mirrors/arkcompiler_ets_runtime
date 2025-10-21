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

#ifndef ECMASCRIPT_STRING_COMPOSITE_BASE_CLASS_H
#define ECMASCRIPT_STRING_COMPOSITE_BASE_CLASS_H

#include "objects/utils/field_macro.h"
#include "base_string_class.h"
#include "objects/base_object.h"
#include "heap/heap_visitor.h"
#include <array>
#include <atomic>
#include <functional>

// step 1. Allocate CompositeBaseClass instance in 1.0. The BaseObject of it is taggedobject in 1.0. It will be this:
// +------------------------------------------------+
// | CompositeBaseClass                             |
// +================================================+
// | BaseObject                                     |
// | +--------------------------------------------+ |
// | | BaseStateWord state_                       | |
// | | +------------------------+---------------+ | |
// | | | classAddress = corresponding hclass    | | | ----> CompositeBaseClassHClass
// | | | language_ = dynamic                    | | |       (an hclass to represent this created in 1.0, )
// | | +------------------------+---------------+ | |
// +------------------------------------------------+
// | padding (uint64_t, optional via ARK_HYBRID)    |
// +------------------------------------------------+
// | BaseClass class_                               |
// | +--------------------------+-----------------+ |
// | | uint64_t header_         | (unused)        | |
// | +--------------------------+-----------------+ |
// | | uint64_t bitfield_     = 0x01 (LINE_STRING)| |
// | +--------------------------------------------+ |
// +------------------------------------------------+
// | ... padding up to SIZE = 1408 bytes            |
// +------------------------------------------------+
//
//
// step 2. In 1.0 runtime. The BaseClass(used as hclass) part will be filled as hclass need
// +------------------------------------------------+
// | CompositeBaseClass                             |
// +================================================+
// | BaseObject                                     |
// | +--------------------------------------------+ |
// | | BaseStateWord state_                       | |
// | | +------------------------+---------------+ | |
// | | | classAddress = corresponding hclass    | | | ----> CompositeBaseClassHClass
// | | | language_ = dynamic                    | | |       (an hclass to represent this created in 1.0, )
// | | +------------------------+---------------+ | |
// +------------------------------------------------+
// | padding (uint64_t, optional via ARK_HYBRID)    |
// +------------------------------------------------+
// | BaseClass class_                               |
// | +--------------------------+-----------------+ |
// | | uint64_t header_ | hclass of hclass in 1.0 | | ----> point to the hclass of hclass in 1.0.
// | +--------------------------+-----------------+ |
// | | uint64_t bitfield_  = 0x01 (LINE_STRING)   | | ----> other bitfield is also filled as 1.0 need.
// | +--------------------------------------------+ |
// +------------------------------------------------+
// | ... padding up to SIZE = 1408 bytes            |
// +------------------------------------------------+
//
//
// (optional)step 3. In 1.2 runtime. The whole will be filled as 1.2 need,and the BaseObject part will be changed.
// Then CompositeBaseClass can be used as 1.2 class.
// +------------------------------------------------+
// | CompositeBaseClass                             |
// +================================================+
// | BaseObject                                     |
// | +--------------------------------------------+ |
// | | BaseStateWord state_                       | |
// | | +------------------------+---------------+ | |
// | | | classAddress =  ClassClass in 1.2      | | | ----> ClassClass in 1.2
// | | | language_ = static                     | | | ----> Change to static
// | | +------------------------+---------------+ | |
// +------------------------------------------------+
// | padding (uint64_t, optional via ARK_HYBRID)    | ----> used as 1.2 stateWord
// +------------------------------------------------+
// | BaseClass class_                               |
// | +--------------------------+-----------------+ |
// | | uint64_t header_ | hclass of hclass in 1.0 | | ----> point to the hclass of hclass in 1.0.
// | +--------------------------+-----------------+ |
// | | uint64_t bitfield_  = 0x01 (LINE_STRING)   | | ----> other bitfield is also filled as 1.0 need.
// | +--------------------------------------------+ |
// +------------------------------------------------+
// | ... padding up to SIZE = 1408 bytes            | ----> initialized as 1.2 class field.
// +------------------------------------------------+
//
//
namespace panda::ecmascript {
class CompositeBaseClass : public common::BaseObject {
public:
    CompositeBaseClass() = delete;
    NO_MOVE_SEMANTIC_CC(CompositeBaseClass);
    NO_COPY_SEMANTIC_CC(CompositeBaseClass);
#ifdef ARK_HYBRID
    [[maybe_unused]] uint64_t padding_;
    static constexpr size_t SIZE = 1416; // Note: should be same with 1.2 string Class size
#else
    static constexpr size_t SIZE = 88; // Note: At least same with JSHClass::SIZE
#endif
    BaseStringClass class_;
    // These fields are primitive or pointer to root, skip it.
    static constexpr size_t VISIT_BEGIN = sizeof(common::BaseObject) + sizeof(BaseStringClass);
    static constexpr size_t VISIT_END = SIZE;
};

class BaseClassRoots final {
public:
    using CompositeBaseClassAllocator = const std::function<CompositeBaseClass *()>;
    BaseClassRoots() = default;
    ~BaseClassRoots() = default;

    NO_COPY_SEMANTIC_CC(BaseClassRoots);
    NO_MOVE_SEMANTIC_CC(BaseClassRoots);

    enum ClassIndex {
        LINE_STRING_CLASS = 0,
        SLICED_STRING_CLASS = 1,
        TREE_STRING_CLASS = 2,
        CACHED_EXTERNAL_STRING_CLASS = 3,
        CLASS_COUNT = 4,
    };

    BaseStringClass *GetBaseClass(EcmaStringType type) const;

    void IterateCompositeBaseClass(const common::RefFieldVisitor &visitorFunc);

    void InitializeCompositeBaseClass(CompositeBaseClassAllocator &allocator);

    void Init() {};

    void Fini()
    {
        initialized_ = false;
        compositeBaseClasses_ = {};
        baseClasses_ = {};
    }

private:
    constexpr static size_t ObjectTypeCount = static_cast<size_t>(EcmaStringType::LAST_OBJECT_TYPE) + 1;
    constexpr static std::array<ClassIndex, ObjectTypeCount> TypeToIndex = [] {
        std::array<ClassIndex, ObjectTypeCount> res{};
        res[static_cast<size_t>(EcmaStringType::LINE_STRING)] = LINE_STRING_CLASS;
        res[static_cast<size_t>(EcmaStringType::SLICED_STRING)] = SLICED_STRING_CLASS;
        res[static_cast<size_t>(EcmaStringType::TREE_STRING)] = TREE_STRING_CLASS;
        res[static_cast<size_t>(EcmaStringType::CACHED_EXTERNAL_STRING)] = CACHED_EXTERNAL_STRING_CLASS;
        return res;
    }();
    void CreateCompositeBaseClass(panda::ecmascript::EcmaStringType type, CompositeBaseClassAllocator &allocator);
    std::array<CompositeBaseClass *, CLASS_COUNT> compositeBaseClasses_{};
    std::array<BaseStringClass *, CLASS_COUNT> baseClasses_{};
    std::atomic_bool initialized_ = false;
};
} // namespace panda::ecmascript
#endif //ECMASCRIPT_STRING_COMPOSITE_BASE_CLASS_H