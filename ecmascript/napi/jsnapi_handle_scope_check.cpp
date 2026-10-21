/**
 * Copyright (c) 2025-2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if ECMASCRIPT_ENABLE_HANDLE_SCOPE_CHECK

#include <sstream>
#include <string>

#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/global_env_constants.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/js_thread.h"
#include "ecmascript/mem/mem_common.h"
#include "ecmascript/mem/visitor.h"
#include "ecmascript/napi/include/jsnapi_expo.h"
#include "ecmascript/napi/jsnapi_handle_scope_check.h"
#include "ecmascript/runtime.h"

namespace panda {
using ecmascript::EcmaVM;
using ecmascript::GlobalEnv;
using ecmascript::GlobalEnvConstants;
using ecmascript::JSTaggedValue;
using ecmascript::JSThread;
using ecmascript::ObjectSlot;
using ecmascript::Root;
using ecmascript::RootVisitor;
using ecmascript::Runtime;

static std::string AddrToHexString(uintptr_t addr)
{
    std::stringstream ss;
    ss << std::hex << addr;
    return ss.str();
}

static bool IsAddrInHalfOpenRange(uintptr_t addr, uintptr_t start, uintptr_t end)
{
    return addr >= start && addr < end;
}

static bool IsAddrInClosedRange(uintptr_t addr, uintptr_t start, uintptr_t end)
{
    return addr >= start && addr <= end;
}

static bool IsAddrInGlobalEnvRange(uintptr_t addr, GlobalEnv *globalEnv)
{
    if (reinterpret_cast<uintptr_t>(globalEnv) == JSTaggedValue::Hole();) {
        return false;
    }
    uintptr_t start = globalEnv->ComputeObjectAddress(0);
    uintptr_t end = globalEnv->ComputeObjectAddress(GlobalEnv::FINAL_INDEX - 1);
    return IsAddrInClosedRange(addr, start, end);
}

static void CollectHandleRangeInfo(std::string &s)
{
    auto toString = [](uintptr_t x) { return AddrToHexString(x); };
    Runtime *runtime = Runtime::GetInstance();
    PrintVisitor<decltype(toString)> visitor(s, toString);
    runtime->GCIterateThreadList([&s, &toString, &visitor](JSThread *thread) {
        s += "thread:" + std::to_string(thread->GetThreadId()) + ":handle:";
        thread->GetEcmaVM()->IterateHandle(visitor);
        const GlobalEnvConstants *globalConst = thread->GlobalConstants();
        uintptr_t start = globalConst->GetGlobalConstantAddr(ecmascript::ConstantIndex::CONSTANT_BEGIN);
        uintptr_t end = globalConst->GetGlobalConstantAddr(ecmascript::ConstantIndex::CONSTANT_END);
        s += "globalConst:start=" + toString(start) + ",end=" + toString(end) + ";";
    });
}

static void CollectStackAndEnvRangeInfo(std::string &ss)
{
    Runtime *runtime = Runtime::GetInstance();
    runtime->GCIterateThreadList([&ss](JSThread *thread) {
        uintptr_t frameStart = reinterpret_cast<uintptr_t>(thread->GetFrameBase());
        size_t frameBufferSize = sizeof(JSTaggedType) *
            thread->GetEcmaVM()->GetEcmaParamConfiguration().GetMaxStackSize();
        uintptr_t frameEnd = frameStart + frameBufferSize;
        ss += "start=" + AddrToHexString(frameStart) + ",end=" + AddrToHexString(frameEnd) + ";";
        uintptr_t stackStart = thread->GetStackLimit();
        uintptr_t stackEnd = thread->GetStackStart();
        ss += "start=" + AddrToHexString(stackStart) + ",end=" + AddrToHexString(stackEnd) + ";";
        GlobalEnv *globalEnv = thread->GetGlobalEnvRaw();
        uintptr_t gStart = globalEnv->ComputeObjectAddress(0);
        uintptr_t gEnd = globalEnv->ComputeObjectAddress(GlobalEnv::FINAL_INDEX - 1);
        ss += "globalEnv start=" + AddrToHexString(gStart) + ",end=" + AddrToHexString(gEnd) + ";";
    });
}

void CheckHandleValid(uintptr_t handle)
{
    if (CheckAddrValid(handle)) {
        return;
    }
    std::string handleInfo;
    CollectHandleRangeInfo(handleInfo);
    std::string stackInfo;
    CollectStackAndEnvRangeInfo(stackInfo);
    LOG_FULL(FATAL) << "invalid address as handle:" << std::hex << handle << "=====" <<
        stackInfo << "------------------------------" << handleInfo;
    UNREACHABLE();
}

static bool CheckGlobalEnv(uintptr_t addr)
{
    JSThread *thread = JSThread::GetCurrent();
    if (IsAddrInGlobalEnvRange(addr, thread->GetGlobalEnvRaw())) {
        return true;
    }
    GlobalEnv *globalEnv = Runtime::GetInstance()->GetGlobalEnvRaw();
    if (IsAddrInGlobalEnvRange(addr, globalEnv)) {
        return true;
    }
    bool good = false;
    thread->GetEcmaVM()->IterateGlobalEnv([addr, &good](JSTaggedValue val) {
        GlobalEnv *env = reinterpret_cast<GlobalEnv *>(val.GetTaggedObject());
        if (IsAddrInGlobalEnvRange(addr, env)) {
            good = true;
        }
    });
    return good;
}

static bool CheckThreadStructAddr(uintptr_t addr, JSThread *thread)
{
    uintptr_t start = reinterpret_cast<uintptr_t>(thread->GetEcmaVM());
    uintptr_t end = start + sizeof(EcmaVM);
    if (IsAddrInHalfOpenRange(addr, start, end)) {
        return true;
    }
    start = reinterpret_cast<uintptr_t>(thread);
    end = start + sizeof(JSThread);
    return IsAddrInHalfOpenRange(addr, start, end);
}

static bool CheckHandleAndPrimitiveAddr(uintptr_t addr, JSThread *thread)
{
    bool good = false;
    CheckVisitor visitor(addr, good);
    thread->GetEcmaVM()->IterateHandle(visitor);
    if (good) {
        return true;
    }
    thread->GetEcmaVM()->IteratePrimitive(visitor);
    return good;
}

static bool CheckStackAddr(uintptr_t addr, JSThread *thread)
{
    uintptr_t start = reinterpret_cast<uintptr_t>(thread->GetFrameBase());
    size_t frameBufferSize = sizeof(JSTaggedType) *
        thread->GetEcmaVM()->GetEcmaParamConfiguration().GetMaxStackSize();
    uintptr_t end = start + frameBufferSize;
    if (IsAddrInClosedRange(addr, start, end)) {
        return true;
    }
    start = thread->GetStackLimit();
    return addr >= start;
}

static bool CheckThreadRelatedAddr(uintptr_t addr)
{
    JSThread *thread = JSThread::GetCurrent();
    if (CheckThreadStructAddr(addr, thread)) {
        return true;
    }
    if (CheckHandleAndPrimitiveAddr(addr, thread)) {
        return true;
    }
    return CheckStackAddr(addr, thread);
}

static bool CheckGlobalConstantsAddr(uintptr_t addr, JSThread *thread)
{
    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    uintptr_t start = globalConst->GetGlobalConstantAddr(ecmascript::ConstantIndex::CONSTANT_BEGIN);
    uintptr_t end = globalConst->GetGlobalConstantAddr(ecmascript::ConstantIndex::CONSTANT_END);
    return IsAddrInHalfOpenRange(addr, start, end);
}

static bool CheckExternalStringTableAddr(uintptr_t addr)
{
    Runtime *runtime = Runtime::GetInstance();
    uintptr_t start = reinterpret_cast<uintptr_t>(runtime->externalRegisteredStringTable_);
    uintptr_t end = reinterpret_cast<uintptr_t>(runtime->externalRegisteredStringTable_) +
        sizeof(JSTaggedValue) * runtime->registeredStringTableSize_;
    return IsAddrInHalfOpenRange(addr, start, end);
}

bool CheckAddrValid(uintptr_t addr)
{
    if (addr == 0) {
        return true;
    }
    if (addr % sizeof(JSTaggedType) != 0) {
        return false;
    }
    JSThread *thread = JSThread::GetCurrent();
    if (thread == nullptr || !thread->IsJSThread()) {
        return true;
    }
    if (CheckGlobalConstantsAddr(addr, thread)) {
        return true;
    }
    if (CheckGlobalEnv(addr)) {
        return true;
    }
    if (CheckExternalStringTableAddr(addr)) {
        return true;
    }
    if (CheckThreadRelatedAddr(addr)) {
        return true;
    }
    bool good = false;
    CheckVisitor visitor(addr, good);
    thread->IterateGlobal(visitor);
    return good;
}
}  // namespace panda

#endif  // ECMASCRIPT_ENABLE_HANDLE_SCOPE_CHECK
