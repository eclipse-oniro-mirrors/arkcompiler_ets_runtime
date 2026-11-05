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

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-pgo-profiler=true

//! METHOD spreadAndRead
//! COUNT CallRuntime StArraySpread 1
function spreadAndRead(holder, source) {
    const before = holder.value;
    const result = [...source];
    return before + holder.value + result.length;
}

class Base {
    constructor(holder, value) {
        holder.value = value;
    }
}

//! METHOD SpreadChild
//! HAS BranchIfFastSpreadArray
//! HAS CallBaseConstructorSpread
//! COUNT CallRuntime SuperCallSpreadWithNewTarget 1
class SpreadChild extends Base {
    constructor(holder, source) {
        const before = holder.value;
        super(...source);
        this.sum = before + holder.value;
    }
}

function check(condition, message) {
    if (!condition) throw new Error(message);
}

const holder = { value: 2 };
const slowStoreSource = {
    [Symbol.iterator]() {
        let done = false;
        return {
            next() {
                if (done) return { done: true };
                done = true;
                holder.value = 9;
                return { value: 1, done: false };
            }
        };
    }
};
const slowCtorSource = {
    [Symbol.iterator]() {
        let index = 0;
        return {
            next() {
                if (index++ === 0) return { value: holder, done: false };
                if (index === 2) return { value: 11, done: false };
                return { done: true };
            }
        };
    }
};
for (let i = 0; i < 24; ++i) {
    holder.value = 2;
    spreadAndRead(holder, i & 1 ? [1, 2] : slowStoreSource);
    holder.value = 2;
    new SpreadChild(holder, i & 1 ? [holder, 7] : slowCtorSource);
}
for (const fn of [spreadAndRead, SpreadChild]) {
    ArkTools.arkSteedCompileSync(fn);
    ArkTools.waitJitCompileFinish(fn);
    check(ArkTools.arkSteedIsCompiled(fn), fn.name + " compile");
}
for (let i = 0; i < 8; ++i) {
    holder.value = 2;
    check(spreadAndRead(holder, [1, 2]) === 6, "spread fast facts");
    holder.value = 2;
    check(spreadAndRead(holder, slowStoreSource) === 12, "iterator mutation");
    holder.value = 2;
    check(new SpreadChild(holder, [holder, 7]).sum === 9, "super fast facts");
    holder.value = 2;
    check(new SpreadChild(holder, slowCtorSource).sum === 13, "super slow facts");
}
for (const fn of [spreadAndRead, SpreadChild]) {
    check(ArkTools.arkSteedIsCompiled(fn), fn.name + " stays compiled");
}
print("PASS");
