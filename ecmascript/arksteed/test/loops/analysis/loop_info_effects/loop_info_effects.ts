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

// @ts-nocheck
declare function print(value: any): void;

//! METHOD loopWithStores
//! HAS JumpLoop
function loopWithStores(holder, values, count)
{
    let sum = 0;
    for (let i = 0; i < count; i++) {
        holder.value = i;
        values[i & 3] = holder;
        sum += i;
    }
    return sum;
}

//! METHOD nestedLoopWithCall
//! COUNT_GE JumpLoop 2
function nestedLoopWithCall(holder, callback, outerCount, innerCount)
{
    let sum = 0;
    for (let i = 0; i < outerCount; i++) {
        for (let j = 0; j < innerCount; j++) {
            holder.other = j;
            sum += callback(i + j);
        }
    }
    return sum;
}

function identity(value)
{
    return value;
}

let holder = { value: 0, other: 0 };
let values = [null, null, null, null];
for (let i = 0; i < 20000; i++) {
    loopWithStores(holder, values, 4);
    nestedLoopWithCall(holder, identity, 2, 2);
}

ArkTools.arkSteedCompileSync(loopWithStores);
ArkTools.arkSteedCompileSync(nestedLoopWithCall);
ArkTools.waitJitCompileFinish(loopWithStores);
ArkTools.waitJitCompileFinish(nestedLoopWithCall);

let passed = ArkTools.isAOTCompiled(loopWithStores) && ArkTools.isAOTCompiled(nestedLoopWithCall);
passed = passed && loopWithStores(holder, values, 4) === 6;
passed = passed && nestedLoopWithCall(holder, identity, 2, 2) === 4;
passed = passed && holder.value === 3 && holder.other === 1;
passed = passed && values[0] === holder && values[3] === holder;
print(passed ? "PASS" : "FAIL");
