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

//! PARAMS --compiler-jit-hotness-threshold=60000

function check(value, message) {
    if (!value) throw new Error(message);
}
function Padded(a = 8, b = 9) {
    this.a = a;
    this.b = b;
    this.count = arguments.length;
    this.extra = arguments[2];
}
function zero() {
    return new Padded();
}
function one(x) {
    return new Padded(x);
}
function three(x) {
    return new Padded(x, x + 1, x + 2);
}
for (let i = 0; i < 32; i++) {
    zero();
    one(i);
    three(i);
}
ArkTools.arkSteedCompileSync(Padded);
ArkTools.arkSteedCompileSync(zero);
ArkTools.arkSteedCompileSync(one);
ArkTools.arkSteedCompileSync(three);
check(
    ArkTools.arkSteedIsCompiled(Padded) &&
        ArkTools.arkSteedIsCompiled(zero) &&
        ArkTools.arkSteedIsCompiled(one) &&
        ArkTools.arkSteedIsCompiled(three),
    "compiled padding entries",
);
for (let i = 0; i < 64; i++) {
    const a = zero();
    const b = one(i);
    const c = three(i);
    check(a.a === 8 && a.b === 9 && a.count === 0, "zero padding");
    check(b.a === i && b.b === 9 && b.count === 1, "one padding");
    check(c.a === i && c.b === i + 1 && c.extra === i + 2 && c.count === 3, "extra arg retained");
}
print("direct_arg_padding passed");
