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

#ifndef ECMASCRIPT_MODULE_ROOT_VISITOR_H
#define ECMASCRIPT_MODULE_ROOT_VISITOR_H

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>

namespace panda::ecmascript {
class GCRoot;

class ModuleRootVisitor {
public:
    ModuleRootVisitor(const ModuleRootVisitor &) = default;
    ModuleRootVisitor &operator=(const ModuleRootVisitor &) = default;

    template <class Visitor,
              std::enable_if_t<!std::is_same_v<std::decay_t<Visitor>, ModuleRootVisitor>, int> = 0>
    explicit ModuleRootVisitor(Visitor &visitor)
        : visitor_(std::addressof(visitor)),
          visit_([](void *visitor, GCRoot &root) { (*static_cast<Visitor *>(visitor))(root); })
    {
    }

    void Visit(GCRoot &root) const
    {
        visit_(visitor_, root);
    }

    void operator()(GCRoot &root) const
    {
        Visit(root);
    }

private:
    void *visitor_ {nullptr};
    void (*visit_)(void *, GCRoot &) {nullptr};
};

using ModuleRootRangeScanner = std::function<void(const ModuleRootVisitor &)>;
using ModuleRootRangeVisitor = std::function<void(uint32_t, const ModuleRootRangeScanner &)>;
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_MODULE_ROOT_VISITOR_H
