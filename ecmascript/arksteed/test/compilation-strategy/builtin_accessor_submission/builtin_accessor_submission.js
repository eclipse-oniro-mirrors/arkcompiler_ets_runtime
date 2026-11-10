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

//! PARAMS --compiler-jit-hotness-threshold=20 --compiler-jit-ic-feedback-threshold=20 --compiler-max-inline-bytecodes=0

let current = 0;
let stored = 0;
const source = {
    get value() { return current + 1; }
};
const receiver = {
    set value(x) { stored = x * 2; }
};
const getter = Object.getOwnPropertyDescriptor(source, "value").get;
const setter = Object.getOwnPropertyDescriptor(receiver, "value").set;
print(ArkTools.arkSteedIsCompiled(getter));
print(ArkTools.arkSteedIsCompiled(setter));
let sum = 0;
for (let i = 0; i < 5000; i++) {
    current = i;
    // Object.assign invokes CALL_GETTER with an empty PGO callback.
    sum += Object.assign({}, source).value;
    // Three-argument Reflect.set invokes CALL_SETTER with an empty PGO callback.
    Reflect.set(receiver, "value", i);
}
ArkTools.waitAllJitCompileFinish();
print(sum);
print(stored);
print(ArkTools.arkSteedIsCompiled(getter));
print(ArkTools.arkSteedIsCompiled(setter));
