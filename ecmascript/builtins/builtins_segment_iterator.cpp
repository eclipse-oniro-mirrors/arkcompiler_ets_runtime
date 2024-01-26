/*
 * Copyright (c) 2023 Shenzhen Kaihong Digital Industry Development Co., Ltd.
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

#include "builtins_segment_iterator.h"

#include "ecmascript/intl/locale_helper.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/global_env.h"
#include "ecmascript/js_intl.h"
#include "ecmascript/js_locale.h"
#include "ecmascript/js_object.h"
#include "ecmascript/js_segment_iterator.h"
#include "ecmascript/object_factory.h"

namespace panda::ecmascript::builtins {
// %SegmentIteratorPrototype%.next ( )
JSTaggedValue BuiltinsSegmentIterator::Next(EcmaRuntimeCallInfo *argv)
{
    JSThread *thread = argv->GetThread();
    BUILTINS_API_TRACE(thread, SegmentIterator, Next);
    [[maybe_unused]] EcmaHandleScope scope(thread);

    // 1. Let iterator be the this value.
    JSHandle<JSTaggedValue> thisValue = GetThis(argv);

    // 2. Perform ? RequireInternalSlot(iterator, [[IteratingSegmenter]]).
    if (!thisValue->IsJSSegmentIterator()) {
        THROW_TYPE_ERROR_AND_RETURN(thread, "this is not SegmentIterator object", JSTaggedValue::Exception());
    }

    JSHandle<JSSegmentIterator> iterator = JSHandle<JSSegmentIterator>::Cast(thisValue);
    return JSSegmentIterator::Next(thread, iterator);
}
}  // namespace panda::ecmascript::builtins