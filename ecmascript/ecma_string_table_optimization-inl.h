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

#ifndef ECMASCRIPT_STRING_TABLE_OPTIMIZ_INL_H
#define ECMASCRIPT_STRING_TABLE_OPTIMIZ_INL_H

#include "ecmascript/ecma_string.h"
#include "ecmascript/ecma_string_table.h"
#include "ecmascript/runtime_lock.h"
#include "ecmascript/string/chained_hash_map-inl.h"

namespace panda::ecmascript {
template <typename LoaderCallback, typename EqualsCallback>
EcmaString* EcmaStringTableImpl::GetOrInternString(EcmaVM* vm, uint32_t hashcode, LoaderCallback loaderCallback,
                                                   EqualsCallback equalsCallback)
{
    ChainedHashMapOperation<ChainedHashMapConfig::NoSlotBarrier> chainedHashMapOperation(GetChainedHashMap());
    ChainedHashMapInUseScope mapInUse(GetChainedHashMap());
    JSThread* holder = vm->GetJSThread();
    BaseString *result = chainedHashMapOperation.template LoadOrStore<true>(holder, hashcode, loaderCallback,
                                                                         equalsCallback);
    ASSERT(result != nullptr);
    return EcmaString::FromBaseString(result);
}

template <typename LoaderCallback, typename EqualsCallback>
EcmaString* EcmaStringTable::GetOrInternString(EcmaVM* vm, uint32_t hashcode, LoaderCallback loaderCallback,
                                               EqualsCallback equalsCallback)
{
    return impl_.GetOrInternString(vm, hashcode, loaderCallback, equalsCallback);
}
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_STRING_TABLE_OPTIMIZ_INL_H
