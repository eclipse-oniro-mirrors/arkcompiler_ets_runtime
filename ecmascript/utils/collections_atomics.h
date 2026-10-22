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

#ifndef ECMASCRIPT_UTILS_COLLECTIONS_ATOMICS_H
#define ECMASCRIPT_UTILS_COLLECTIONS_ATOMICS_H

#include "ecmascript/builtins/builtins_atomics.h"
#include "ecmascript/utils/collections_atomic_helper.h"

namespace panda::ecmascript::collections {
class CollectionsAtomics : public builtins::BuiltinsAtomics {
public:
    static JSTaggedValue Add(EcmaRuntimeCallInfo *argv);
    static JSTaggedValue Sub(EcmaRuntimeCallInfo *argv);
    static JSTaggedValue And(EcmaRuntimeCallInfo *argv);
    static JSTaggedValue Or(EcmaRuntimeCallInfo *argv);
    static JSTaggedValue Xor(EcmaRuntimeCallInfo *argv);
    static JSTaggedValue Exchange(EcmaRuntimeCallInfo *argv);
    static JSTaggedValue CompareExchange(EcmaRuntimeCallInfo *argv);
    static JSTaggedValue Load(EcmaRuntimeCallInfo *argv);
    static JSTaggedValue Store(EcmaRuntimeCallInfo *argv);
    static JSTaggedValue Wait(EcmaRuntimeCallInfo *argv);
    static JSTaggedValue Notify(EcmaRuntimeCallInfo *argv);
    static Span<const base::BuiltinFunctionEntry> GetAtomicsFunctions()
    {
        return Span<const base::BuiltinFunctionEntry>(ATOMICS_FUNCTIONS);
    }
private:
    static constexpr int IS_LOCK_FREE_LENGTH = 1;
    static constexpr int LOAD_LENGTH = 2;
    static constexpr int UPDATE_LENGTH = 3;
    static constexpr int COMPARE_EXCHANGE_LENGTH = 4;
    static constexpr int WAIT_LENGTH = 4;
    static constexpr auto INVALID_BUILTIN_ID = kungfu::BuiltinsStubCSigns::ID::INVALID;
    static constexpr std::array ATOMICS_FUNCTIONS = {
        base::BuiltinFunctionEntry::Create("add", Add, UPDATE_LENGTH, INVALID_BUILTIN_ID),
        base::BuiltinFunctionEntry::Create("and", And, UPDATE_LENGTH, INVALID_BUILTIN_ID),
        base::BuiltinFunctionEntry::Create("compareExchange", CompareExchange,
            COMPARE_EXCHANGE_LENGTH, INVALID_BUILTIN_ID),
        base::BuiltinFunctionEntry::Create("exchange", Exchange, UPDATE_LENGTH, INVALID_BUILTIN_ID),
        base::BuiltinFunctionEntry::Create("isLockFree", IsLockFree, IS_LOCK_FREE_LENGTH, INVALID_BUILTIN_ID),
        base::BuiltinFunctionEntry::Create("load", Load, LOAD_LENGTH, INVALID_BUILTIN_ID),
        base::BuiltinFunctionEntry::Create("notify", Notify, UPDATE_LENGTH, INVALID_BUILTIN_ID),
        base::BuiltinFunctionEntry::Create("or", Or, UPDATE_LENGTH, INVALID_BUILTIN_ID),
        base::BuiltinFunctionEntry::Create("store", Store, UPDATE_LENGTH, INVALID_BUILTIN_ID),
        base::BuiltinFunctionEntry::Create("sub", Sub, UPDATE_LENGTH, INVALID_BUILTIN_ID),
        base::BuiltinFunctionEntry::Create("wait", Wait, WAIT_LENGTH, INVALID_BUILTIN_ID),
        base::BuiltinFunctionEntry::Create("xor", Xor, UPDATE_LENGTH, INVALID_BUILTIN_ID),
    };
    template<typename Op>
    static JSTaggedValue AtomicReadModifyWrite(JSThread *thread, const JSHandle<JSTaggedValue> &array,
        JSHandle<JSTaggedValue> &index, EcmaRuntimeCallInfo *argv, const Op &op);
    template<typename T, typename Op>
    static JSTaggedValue ModifyInteger(JSThread *thread, const JSHandle<JSTaggedValue> &array,
        uint32_t offset, EcmaRuntimeCallInfo *argv, const Op &op);
    static builtins::WaitResult DoWait(JSThread *thread, const JSHandle<JSTaggedValue> &array,
        size_t index, int32_t expected, double timeout);
    static builtins::WaitResult WaitOnNode(JSThread *thread, WaiterListNode *node, double timeout);
    static uint32_t SignalShared(JSThread *thread, const JSHandle<JSTaggedValue> &array,
        size_t index, double wakeCount);
    static double ConvertTimeout(JSThread *thread, const JSHandle<JSTaggedValue> &timeout);
};
}  // namespace panda::ecmascript::collections
#endif  // ECMASCRIPT_UTILS_COLLECTIONS_ATOMICS_H
