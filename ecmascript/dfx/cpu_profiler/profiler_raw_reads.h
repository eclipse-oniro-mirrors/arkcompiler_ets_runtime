/**
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

#ifndef ECMASCRIPT_DFX_CPU_PROFILER_PROFILER_RAW_READS_H
#define ECMASCRIPT_DFX_CPU_PROFILER_PROFILER_RAW_READS_H

#include "ecmascript/js_function.h"
#include "ecmascript/js_proxy.h"
#include "ecmascript/mem/barriers.h"

namespace panda::ecmascript {
// No-barrier raw field reads, private to the CPU profiler module (plan §6.0).
// A barriered tagged read can synchronously enter CC-GC evacuation, which
// allocates and takes locks -- a same-thread reentry deadlock when the
// interrupted code holds those locks (issue #13618, gate bridge D1). Raw
// from-space values stay readable for the rest of the current cycle, costing
// at most one sampling period of staleness, which the consumer-side
// live-file check already tolerates.
inline Method *GetMethodFieldNoBarrier(const TaggedObject *obj)
{
    static_assert(JSFunctionBase::METHOD_OFFSET == JSProxy::METHOD_OFFSET);
    JSTaggedType raw = Barriers::GetPrimitive<JSTaggedType>(obj, JSFunctionBase::METHOD_OFFSET);
    return static_cast<Method *>(JSTaggedValue(raw).GetTaggedObject());
}

inline JSTaggedValue GetNativeFunctionExtraInfoNoBarrier(const TaggedObject *obj)
{
    JSTaggedType raw = Barriers::GetPrimitive<JSTaggedType>(obj, ECMAObject::HASH_OFFSET);
    JSTaggedValue value(raw);
    if (value.CheckIsJSNativePointer()) {
        return value;
    }
    return JSTaggedValue::Undefined();
}
} // namespace panda::ecmascript
#endif // ECMASCRIPT_DFX_CPU_PROFILER_PROFILER_RAW_READS_H
