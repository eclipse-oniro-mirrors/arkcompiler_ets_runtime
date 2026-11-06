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

// Test: shared out-of-line pending-exception exit.
// Multiple throwing call sites without a catch block share ONE exit.

//! PARAMS --asm-interpreter=true --compiler-jit-hotness-threshold=0 --compiler-enable-baselinejit=false --enable-cmc-gc=false
//! METHOD multipleThrowingCalls
//! CODEGEN_COUNT *Return with pending exception* 1

function multipleThrowingCalls(a, b) {
    let obj = { val: a };
    let x = obj.val;
    let y = x + b;
    let obj2 = { val: y };
    let z = obj2.val * 2;
    let s = "abc";
    let n = s.length;
    return z + n + a;
}

ArkTools.arkSteedCompileSync(multipleThrowingCalls);
print(multipleThrowingCalls(1, 2));
