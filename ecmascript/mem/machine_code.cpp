/*
 * Copyright (c) 2023-2024 Huawei Device Co., Ltd.
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

#include "ecmascript/mem/machine_code.h"
#include "ecmascript/mem/assert_scope.h"
#include "ecmascript/compiler/aot_file/func_entry_des.h"
#include "ecmascript/stackmap/ark_stackmap.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_tagged_value-inl.h"
#ifdef CODE_SIGN_ENABLE
#include "ecmascript/jit/jit.h"
#include "jit_buffer_integrity.h"
#endif

namespace panda::ecmascript {
#ifdef CODE_SIGN_ENABLE
using namespace OHOS::Security::CodeSign;
#endif

void MachineCode::SetData(const MachineCodeDesc &desc, JSHandle<Method> &method, size_t dataSize)
{
    DISALLOW_GARBAGE_COLLECTION;
    if (desc.codeType == MachineCodeType::BASELINE_CODE) {
        SetBaselineCodeData(desc, method, dataSize);
        return;
    }
    EntityId methodId = method->GetMethodId();

    SetOSROffset(MachineCode::INVALID_OSR_OFFSET);
    SetOsrDeoptFlag(false);
    SetOsrExecuteCnt(0);

    size_t rodataSizeBeforeTextAlign = AlignUp(desc.rodataSizeBeforeText, MachineCode::TEXT_ALIGN);
    size_t codeSizeAlign = AlignUp(desc.codeSize, MachineCode::DATA_ALIGN);
#ifdef ENABLE_JITFORT
    size_t rodataSizeAfterTextAlign = AlignUp(desc.rodataSizeAfterText, MachineCode::TEXT_ALIGN); // match jit_task code
#else
    size_t rodataSizeAfterTextAlign = AlignUp(desc.rodataSizeAfterText, MachineCode::DATA_ALIGN);
#endif

    size_t funcEntryDesSizeAlign = AlignUp(desc.funcEntryDesSize, MachineCode::TEXT_ALIGN);
    SetFuncEntryDesSize(funcEntryDesSizeAlign);

    size_t instrSize = rodataSizeBeforeTextAlign + codeSizeAlign + rodataSizeAfterTextAlign;
    SetInstructionsSize(instrSize);

    size_t stackMapOrOffsetTableSizeAlign = AlignUp(desc.stackMapOrOffsetTableSize, MachineCode::DATA_ALIGN);
    SetStackMapOrOffsetTableSize(stackMapOrOffsetTableSizeAlign);

#ifdef ENABLE_JITFORT
    ASSERT(desc.instructionsAddr != 0);
    ASSERT(dataSize == (funcEntryDesSizeAlign + stackMapOrOffsetTableSizeAlign) ||
           dataSize == (funcEntryDesSizeAlign + instrSize + stackMapOrOffsetTableSizeAlign));
    SetInstructionsAddr(desc.instructionsAddr);
#else
    ASSERT(dataSize == (funcEntryDesSizeAlign + instrSize + stackMapOrOffsetTableSizeAlign));
#endif
    SetPayLoadSizeInBytes(dataSize);

    uint8_t *textStart = reinterpret_cast<uint8_t*>(GetText());
    uint8_t *pText = textStart;
    if (rodataSizeBeforeTextAlign != 0) {
        if (memcpy_s(pText, rodataSizeBeforeTextAlign,
            reinterpret_cast<uint8_t*>(desc.rodataAddrBeforeText), desc.rodataSizeBeforeText) != EOK) {
            LOG_JIT(ERROR) << "memcpy fail in copy fast jit code";
            return;
        }
        pText += rodataSizeBeforeTextAlign;
    }
#ifdef CODE_SIGN_ENABLE
    if ((uintptr_t)desc.codeSigner == 0) {
        if (memcpy_s(pText, codeSizeAlign, reinterpret_cast<uint8_t*>(desc.codeAddr), desc.codeSize) != EOK) {
            LOG_JIT(ERROR) << "memcpy fail in copy fast jit code";
            return;
        }
    } else {
        LOG_JIT(DEBUG) << "Call JitVerifyAndCopy: "
                       << std::hex << (uintptr_t)pText << " <- "
                       << std::hex << (uintptr_t)desc.codeAddr << " size: " << desc.codeSize;
        LOG_JIT(DEBUG) << "     codeSigner = " << std::hex << (uintptr_t)desc.codeSigner;
        if (Jit::GetInstance()->JitVerifyAndCopy(reinterpret_cast<void*>(desc.codeSigner),
            pText, reinterpret_cast<void*>(desc.codeAddr), desc.codeSize) != EOK) {
            LOG_JIT(ERROR) << "     JitVerifyAndCopy failed";
        } else {
            LOG_JIT(DEBUG) << "     JitVerifyAndCopy success!!";
        }
        delete reinterpret_cast<JitCodeSignerBase*>(desc.codeSigner);
    }
#else
    if (memcpy_s(pText, codeSizeAlign, reinterpret_cast<uint8_t*>(desc.codeAddr), desc.codeSize) != EOK) {
        LOG_JIT(ERROR) << "memcpy fail in copy fast jit code";
        return;
    }
#endif
    pText += codeSizeAlign;
    if (rodataSizeAfterTextAlign != 0) {
        if (memcpy_s(pText, rodataSizeAfterTextAlign,
            reinterpret_cast<uint8_t*>(desc.rodataAddrAfterText), desc.rodataSizeAfterText) != EOK) {
            LOG_JIT(ERROR) << "memcpy fail in copy fast jit code";
            return;
        }
    }

    uint8_t *stackmapAddr = GetStackMapOrOffsetTableAddress();
    if (memcpy_s(stackmapAddr, desc.stackMapOrOffsetTableSize,
                 reinterpret_cast<uint8_t*>(desc.stackMapOrOffsetTableAddr),
                 desc.stackMapOrOffsetTableSize) != EOK) {
        LOG_JIT(ERROR) << "memcpy fail in copy fast jit stackmap";
        return;
    }

    FuncEntryDes *funcEntry = reinterpret_cast<FuncEntryDes*>(GetFuncEntryDesAddress());
    if (memcpy_s(funcEntry, desc.funcEntryDesSize, reinterpret_cast<uint8_t*>(desc.funcEntryDesAddr),
        desc.funcEntryDesSize) != EOK) {
        LOG_JIT(ERROR) << "memcpy fail in copy fast jit funcEntry";
        return;
    }

    uint32_t cnt = desc.funcEntryDesSize / sizeof(FuncEntryDes);
    ASSERT(cnt <= 2); // 2: jsfunction + deoptimize stub
    for (uint32_t i = 0; i < cnt; i++) {
        funcEntry->codeAddr_ += reinterpret_cast<uintptr_t>(textStart);
        if (methodId == EntityId(funcEntry->indexInKindOrMethodId_)) {
            SetFuncAddr(funcEntry->codeAddr_);
        }
        funcEntry++;
    }

    CString methodName = method->GetRecordNameStr() + "." + CString(method->GetMethodName());
    LOG_JIT(DEBUG) << "Fast JIT MachineCode :" << methodName << ", "  << " text addr:" <<
        reinterpret_cast<void*>(GetText()) << ", size:" << instrSize  <<
        ", stackMap addr:" << reinterpret_cast<void*>(stackmapAddr) <<
        ", size:" << stackMapOrOffsetTableSizeAlign <<
        ", funcEntry addr:" << reinterpret_cast<void*>(GetFuncEntryDesAddress()) << ", count:" << cnt;

#ifndef ENABLE_JIT_FORT
    //todo
    size_t pageSize = 4096; // 4096 : pageSize
    uintptr_t startPage = reinterpret_cast<uintptr_t>(textStart) & ~(pageSize - 1);
    uintptr_t endPage = (reinterpret_cast<uintptr_t>(textStart) + dataSize) & ~(pageSize - 1);
    size_t protSize = (endPage == startPage) ? ((dataSize + pageSize - 1U) & (~(pageSize - 1))) :
        (pageSize + ((dataSize + pageSize - 1U) & (~(pageSize - 1))));
    PageProtect(reinterpret_cast<void*>(startPage), protSize, PAGE_PROT_EXEC_READWRITE);
#endif
}

void MachineCode::SetBaselineCodeData(const MachineCodeDesc &desc,
                                      JSHandle<Method> &method, size_t dataSize)
{
    DISALLOW_GARBAGE_COLLECTION;
    SetFuncEntryDesSize(0);

    size_t instrSizeAlign = AlignUp(desc.codeSize, MachineCode::DATA_ALIGN);
    SetInstructionsSize(instrSizeAlign);

    size_t stackMapOrOffsetTableSizeAlign = AlignUp(desc.stackMapOrOffsetTableSize, MachineCode::DATA_ALIGN);
    SetStackMapOrOffsetTableSize(stackMapOrOffsetTableSizeAlign);

    ASSERT(dataSize == (instrSizeAlign + stackMapOrOffsetTableSizeAlign));
    SetPayLoadSizeInBytes(dataSize);

    uint8_t *textStart = reinterpret_cast<uint8_t*>(GetText());
    ASSERT(IsAligned(reinterpret_cast<uintptr_t>(textStart), TEXT_ALIGN));
    uint8_t *pText = textStart;
    if (memcpy_s(pText, instrSizeAlign, reinterpret_cast<uint8_t*>(desc.codeAddr), desc.codeSize) != EOK) {
        LOG_BASELINEJIT(ERROR) << "memcpy fail in copy baseline jit code";
        return;
    }
    pText += instrSizeAlign;

    uint8_t *stackmapAddr = GetStackMapOrOffsetTableAddress();
    if (memcpy_s(stackmapAddr, desc.stackMapOrOffsetTableSize,
                 reinterpret_cast<uint8_t*>(desc.stackMapOrOffsetTableAddr),
                 desc.stackMapOrOffsetTableSize) != EOK) {
        LOG_BASELINEJIT(ERROR) << "memcpy fail in copy fast baselineJIT offsetTable";
        return;
    }

    SetFuncAddr(reinterpret_cast<uintptr_t>(textStart));

    CString methodName = method->GetRecordNameStr() + "." + CString(method->GetMethodName());
    LOG_BASELINEJIT(DEBUG) << "BaselineCode :" << methodName << ", "  << " text addr:" <<
        reinterpret_cast<void*>(GetText()) << ", size:" << instrSizeAlign  <<
        ", stackMap addr: 0, size: 0, funcEntry addr: 0, count 0";

#ifndef ENABLE_JITFORT
    //todo
    size_t pageSize = 4096;
    uintptr_t startPage = reinterpret_cast<uintptr_t>(textStart) & ~(pageSize - 1);
    uintptr_t endPage = (reinterpret_cast<uintptr_t>(textStart) + dataSize) & ~(pageSize - 1);
    size_t protSize = (endPage == startPage) ? ((dataSize + pageSize - 1U) & (~(pageSize - 1))) :
        (pageSize + ((dataSize + pageSize - 1U) & (~(pageSize - 1))));
    PageProtect(reinterpret_cast<void*>(startPage), protSize, PAGE_PROT_EXEC_READWRITE);
#endif
}

bool MachineCode::IsInText(const uintptr_t pc) const
{
    uintptr_t textStart = GetText();
    uintptr_t textEnd = textStart + GetTextSize();
    return textStart <= pc && pc < textEnd;
}

uintptr_t MachineCode::GetFuncEntryDes() const
{
    uint32_t funcEntryCnt = GetFuncEntryDesSize() / sizeof(FuncEntryDes);
    FuncEntryDes *funcEntryDes = reinterpret_cast<FuncEntryDes*>(GetFuncEntryDesAddress());
    for (uint32_t i = 0; i < funcEntryCnt; i++) {
        if (funcEntryDes->codeAddr_ == GetFuncAddr()) {
            return reinterpret_cast<uintptr_t>(funcEntryDes);
        }
        funcEntryDes++;
    }
    UNREACHABLE();
    return 0;
}

std::tuple<uint64_t, uint8_t*, int, kungfu::CalleeRegAndOffsetVec> MachineCode::CalCallSiteInfo(uintptr_t retAddr) const
{
    uintptr_t textStart = GetText();
    uint8_t *stackmapAddr = GetStackMapOrOffsetTableAddress();
    ASSERT(stackmapAddr != nullptr);

    uint32_t funcEntryCnt = GetFuncEntryDesSize() / sizeof(FuncEntryDes);
    FuncEntryDes *tmpFuncEntryDes = reinterpret_cast<FuncEntryDes*>(GetFuncEntryDesAddress());
    FuncEntryDes *funcEntryDes = nullptr;
    ASSERT(tmpFuncEntryDes != nullptr);
    uintptr_t pc = retAddr - 1;  // -1: for pc
    for (uint32_t i = 0; i < funcEntryCnt; i++) {
        if (tmpFuncEntryDes->codeAddr_ <= pc && pc < (tmpFuncEntryDes->codeAddr_ + tmpFuncEntryDes->funcSize_)) {
            funcEntryDes = tmpFuncEntryDes;
            break;
        }
        tmpFuncEntryDes++;
    }

    if (funcEntryDes == nullptr) {
        return {};
    }

    int delta = funcEntryDes->fpDeltaPrevFrameSp_;
    kungfu::CalleeRegAndOffsetVec calleeRegInfo;
    for (uint32_t j = 0; j < funcEntryDes->calleeRegisterNum_; j++) {
        kungfu::LLVMStackMapType::DwarfRegType reg =
            static_cast<kungfu::LLVMStackMapType::DwarfRegType>(funcEntryDes->CalleeReg2Offset_[2 * j]);
        kungfu::LLVMStackMapType::OffsetType offset =
            static_cast<kungfu::LLVMStackMapType::OffsetType>(funcEntryDes->CalleeReg2Offset_[2 * j + 1]);
        kungfu::LLVMStackMapType::DwarfRegAndOffsetType regAndOffset = std::make_pair(reg, offset);
        calleeRegInfo.emplace_back(regAndOffset);
    }
    auto ret = std::make_tuple(textStart, stackmapAddr, delta, calleeRegInfo);
    return ret;
}
}  // namespace panda::ecmascript
