/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_PLATFORM_MAP_H
#define ECMASCRIPT_PLATFORM_MAP_H

#include <cstddef>

#include "ecmascript/common.h"
#include "ecmascript/mem/c_string.h"

namespace panda::ecmascript {
class MemMap {
public:
    MemMap() : originAddr_(nullptr), mem_(nullptr), size_(0) {}
    MemMap(void *mem, size_t size) : originAddr_(mem), mem_(mem), size_(size) {};
    MemMap(void *originAddr, void *mem, size_t size) : originAddr_(originAddr), mem_(mem), size_(size) {};
    ~MemMap() = default;

    void Reset()
    {
        originAddr_ = nullptr;
        mem_ = nullptr;
        size_ = 0;
    }

    inline void *GetMem() const
    {
        return mem_;
    }

    inline size_t GetSize() const
    {
        return size_;
    }

    inline void *GetOriginAddr() const
    {
        return originAddr_;
    }
private:
    void *originAddr_ {nullptr};
    void *mem_ {nullptr};
    size_t size_ {0};
};

enum class PageTagType : uint8_t {
    HEAP,
    MACHINE_CODE,
    MEMPOOL_CACHE,
};

#ifdef PANDA_TARGET_WINDOWS
#define PAGE_PROT_NONE 0x01
#define PAGE_PROT_READ 0x02
#define PAGE_PROT_READWRITE 0x04
#define PAGE_PROT_EXEC_READ 0x20
// For safety reason, Disallow prot have both write & exec capability except in JIT.
#define PAGE_PROT_EXEC_READWRITE 0x40
#else
#define PAGE_PROT_NONE 0
#define PAGE_PROT_READ 1
#define PAGE_PROT_READWRITE 3
#define PAGE_PROT_EXEC_READ 5
// For safety reason, Disallow prot have both write & exec capability except in JIT.
#define PAGE_PROT_EXEC_READWRITE 7
#endif

static constexpr char HEAP_TAG[] = "ArkTS Heap";
static constexpr char CODE_TAG[] = "ArkTS Code";
static const std::string EMPTY_STRING = "";
MemMap PUBLIC_API PageMap(size_t size, int prot = PAGE_PROT_NONE, size_t alignment = 0, void *addr = nullptr);
void PUBLIC_API PageUnmap(MemMap it);
MemMap PUBLIC_API MachineCodePageMap(size_t size, int prot = PAGE_PROT_NONE, size_t alignment = 0);
void PUBLIC_API MachineCodePageUnmap(MemMap it);
void PageRelease(void *mem, size_t size);
void PagePreRead(void *mem, size_t size);
void PageTag(void *mem, size_t size, PageTagType type, const std::string &spaceName = EMPTY_STRING,
             const uint32_t threadId = 0);
void PageClearTag(void *mem, size_t size);
const CString GetPageTagString(PageTagType type, const std::string &spaceName, const uint32_t threadId = 0);
void PageProtect(void *mem, size_t size, int prot);
size_t PageSize();
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_PLATFORM_MAP_H
