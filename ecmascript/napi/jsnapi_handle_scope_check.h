/*
 * Copyright (c) 2025-2026 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_NAPI_JSNAPI_HANDLE_SCOPE_CHECK_H
#define ECMASCRIPT_NAPI_JSNAPI_HANDLE_SCOPE_CHECK_H

#include <cstdint>
#include <string>

#if ECMASCRIPT_ENABLE_HANDLE_SCOPE_CHECK
#include "ecmascript/mem/visitor.h"
#endif

namespace panda {
#if ECMASCRIPT_ENABLE_HANDLE_SCOPE_CHECK
template<typename Callback>
class PrintVisitor : public ecmascript::RootVisitor {
public:
    PrintVisitor(std::string &s, Callback &cb) : s_(s), cb_(cb) {}

    void VisitRoot([[maybe_unused]] ecmascript::Root type, [[maybe_unused]] ecmascript::ObjectSlot slot) {}

    void VisitRangeRoot([[maybe_unused]] ecmascript::Root type, ecmascript::ObjectSlot start,
                        ecmascript::ObjectSlot end)
    {
        s_ += "start=" + cb_(start.SlotAddress()) + ",end=" + cb_(end.SlotAddress()) + ";";
    }

    void VisitBaseAndDerivedRoot([[maybe_unused]] ecmascript::Root type, [[maybe_unused]] ecmascript::ObjectSlot base,
                                 [[maybe_unused]] ecmascript::ObjectSlot derived,
                                 [[maybe_unused]] uintptr_t baseOldObject) {}

private:
    std::string &s_;
    Callback &cb_;
};

class CheckVisitor : public ecmascript::RootVisitor {
public:
    CheckVisitor(uintptr_t addr, bool &good) : addr_(addr), good_(good) {}

    void VisitRoot([[maybe_unused]] ecmascript::Root type, ecmascript::ObjectSlot slot)
    {
        if (addr_ == slot.SlotAddress()) {
            good_ = true;
        }
    }

    void VisitRangeRoot([[maybe_unused]] ecmascript::Root type, ecmascript::ObjectSlot start,
                        ecmascript::ObjectSlot end)
    {
        if (addr_ >= start.SlotAddress() && addr_ < end.SlotAddress()) {
            good_ = true;
        }
    }

    void VisitBaseAndDerivedRoot([[maybe_unused]] ecmascript::Root type, [[maybe_unused]] ecmascript::ObjectSlot base,
                                 [[maybe_unused]] ecmascript::ObjectSlot derived,
                                 [[maybe_unused]] uintptr_t baseOldObject) {}

private:
    uintptr_t addr_;
    bool &good_;
};

void CheckHandleValid(uintptr_t handle);
bool CheckAddrValid(uintptr_t addr);
#endif
}  // namespace panda

#endif  // ECMASCRIPT_NAPI_JSNAPI_HANDLE_SCOPE_CHECK_H
