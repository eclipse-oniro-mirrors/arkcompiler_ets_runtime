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

//! PARAMS --compiler-jit-hotness-threshold=6000
//! METHOD callArg0
//! HAS CallKnownJSFunction
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT Call

//! METHOD callArgs3
//! HAS CallKnownJSFunction
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT Call

//! METHOD callRange
//! HAS CallKnownJSFunction
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT Call

//! METHOD callThisRange
//! HAS CallKnownJSFunction
//! COUNT DeoptIfReferenceNotEqual 1
//! HAS_NOT Call

function arg0Target() {
    let result = 41;
    for (let index = 0; index < 1; index++) {
        result++;
    }
    return result;
}

function args3Target(a, b, c) {
    let result = a * 100 + b * 10 + c;
    for (let index = 0; index < 1; index++) {
        result += index;
    }
    return result;
}

function rangeTarget(a, b, c, d) {
    let result = 0;
    for (let index = 0; index < 1; index++) {
        result += a + b + c + d;
    }
    return result;
}

const receiver = {
    base: 100,
    range(a, b, c, d) {
        let result = this.base;
        for (let index = 0; index < 1; index++) {
            result += a + b + c + d;
        }
        return result;
    }
};

function callArg0() {
    return arg0Target();
}

function callArgs3(a, b, c) {
    return args3Target(a, b, c);
}

function callRange(a, b, c, d) {
    return rangeTarget(a, b, c, d);
}

function callThisRange(a, b, c, d) {
    return receiver.range(a, b, c, d);
}

for (let index = 0; index < 200; index++) {
    callArg0();
    callArgs3(1, 2, 3);
    callRange(1, 2, 3, 4);
    callThisRange(1, 2, 3, 4);
}

ArkTools.arkSteedCompileSync(arg0Target);
ArkTools.arkSteedCompileSync(args3Target);
ArkTools.arkSteedCompileSync(rangeTarget);
ArkTools.arkSteedCompileSync(receiver.range);
ArkTools.arkSteedCompileSync(callArg0);
ArkTools.arkSteedCompileSync(callArgs3);
ArkTools.arkSteedCompileSync(callRange);
ArkTools.arkSteedCompileSync(callThisRange);

print(ArkTools.arkSteedIsCompiled(callArg0));
print(ArkTools.arkSteedIsCompiled(callArgs3));
print(ArkTools.arkSteedIsCompiled(callRange));
print(ArkTools.arkSteedIsCompiled(callThisRange));
print(callArg0());
print(callArgs3(1, 2, 3));
print(callRange(1, 2, 3, 4));
print(callThisRange(1, 2, 3, 4));
