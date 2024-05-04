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

#include "ecmascript/compiler/aot_file/aot_file_info.h"

namespace panda::ecmascript {
void AOTFileInfo::Destroy()
{
    entryNum_ = 0;
    moduleNum_ = 0;
    totalCodeSize_ = 0;
    entries_.clear();
    des_.clear();
    ExecutedMemoryAllocator::DestroyBuf(stubsMem_);
}

bool AOTFileInfo::CalCallSiteInfo(uintptr_t retAddr,
                                  std::tuple<uint64_t, uint8_t *, int, CalleeRegAndOffsetVec> &ret) const
{
    uint64_t textStart = 0;
    uint8_t *stackmapAddr = nullptr;
    int delta = 0;
    const auto &des = GetCodeUnits();
    const auto &funcEntryDes = GetStubs();

    auto cmp = [](const AOTFileInfo::FuncEntryDes &a, const AOTFileInfo::FuncEntryDes &b) {
        return a.codeAddr_ < b.codeAddr_;
    };
    size_t len = des.size();
    for (size_t i = 0; i < len; i++) {
        auto &d = des[i];
        uint64_t addr = d.GetSecAddr(ElfSecName::TEXT);
        uint32_t size = d.GetSecSize(ElfSecName::TEXT);
        if (retAddr < addr || retAddr >= addr + size) {
            continue;
        }
        stackmapAddr = d.GetArkStackMapRawPtr();
        ASSERT(stackmapAddr != nullptr);
        textStart = addr;
        auto startIndex = d.GetStartIndex();
        auto funcCount = d.GetFuncCount();
        auto s = funcEntryDes.begin() + startIndex;
        auto t = funcEntryDes.begin() + startIndex + funcCount;
        AOTFileInfo::FuncEntryDes target;
        ASSERT(retAddr > 0);
        target.codeAddr_ = retAddr - 1;  // -1: for pc
        auto it = std::upper_bound(s, t, target, cmp);
        --it;
        ASSERT(it != t);
        ASSERT((it->codeAddr_ <= target.codeAddr_) && (target.codeAddr_ < it->codeAddr_ + it->funcSize_));
        delta = it->fpDeltaPrevFrameSp_;
        CalleeRegAndOffsetVec calleeRegInfo;
        for (uint32_t j = 0; j < it->calleeRegisterNum_; j++) {
            DwarfRegType reg = static_cast<DwarfRegType>(it->CalleeReg2Offset_[2 * j]);
            OffsetType offset = static_cast<OffsetType>(it->CalleeReg2Offset_[2 * j + 1]);
            DwarfRegAndOffsetType regAndOffset = std::make_pair(reg, offset);
            calleeRegInfo.emplace_back(regAndOffset);
        }
        ret = std::make_tuple(textStart, stackmapAddr, delta, calleeRegInfo);
        return true;
    }
    return false;
}
}  // namespace panda::ecmascript
