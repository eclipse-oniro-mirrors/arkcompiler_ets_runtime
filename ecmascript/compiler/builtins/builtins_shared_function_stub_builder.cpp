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

#include "ecmascript/compiler/builtins/builtins_shared_function_stub_builder.h"
#include "ecmascript/compiler/stub_builder-inl.h"

namespace panda::ecmascript::kungfu {
// A class-constructor callee goes to the C++ slowPath to maintain compatibility of error message
void BuiltinsSharedFunctionStubBuilder::PrototypeCall(GateRef glue, GateRef thisValue,
    GateRef numArgs, Variable* res, Label *exit, Label *slowPath)
{
    auto env = GetEnvironment();
    Label thisValueIsHeapObject(env);
    Label notClassCtor(env);
    BRANCH(TaggedIsHeapObject(thisValue), &thisValueIsHeapObject, slowPath);
    Bind(&thisValueIsHeapObject);
    BRANCH(IsClassConstructor(glue, thisValue), slowPath, &notClassCtor);
    Bind(&notClassCtor);
    BuiltinsFunctionStubBuilder::PrototypeCall(glue, thisValue, numArgs, res, exit, slowPath);
}
}  // namespace panda::ecmascript::kungfu
