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

// Test: no shared out-of-line exit when ALL throwing sites are
// covered by a catch block. Uses only local variables so no
// global access (which can throw) leaks outside the try.

//! PARAMS --asm-interpreter=true --compiler-jit-hotness-threshold=0 --compiler-enable-baselinejit=false --enable-cmc-gc=false
//! METHOD safeWithCatch
//! CODEGEN_COUNT *Return with pending exception* 0

function safeWithCatch(x) {
    let cnt = 0;
    try {
        let o = null;
        let v = o.id;
        cnt = v;
    } catch (e) {
        cnt = x;
    }
    return cnt;
}

ArkTools.arkSteedCompileSync(safeWithCatch);
print(safeWithCatch(42));
