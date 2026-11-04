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

// The debug-only force flag complements explicit collections below; CMC is off.
//! PARAMS --asm-interpreter=true --compiler-jit-hotness-threshold=60000 --compiler-enable-baselinejit=false --enable-cmc-gc=false --enable-force-gc=true

//! METHOD createRetained
//! COUNT AllocateInYoung 1
//! BYTECODE_COUNT createobjectwithbuffer 0x0,* 1
//! HAS HeapConstant
//! COUNT InitializeObjectWithBuffer 1
//! HAS_NOT CallCommonStub CreateObjectHavingMethod

//! METHOD createHeapRetained
//! BYTECODE_COUNT createobjectwithbuffer 1
//! COUNT CallCommonStub CreateObjectHavingMethod 1

function createRetained() {
    return { integer: 17, fraction: 2.5, empty: null, flag: true };
}

function createHeapRetained(captured) {
    return { text: "retained-heap-template", read() { return captured; } };
}

function hasValues(object) {
    return object.integer === 17 && object.fraction === 2.5 && object.empty === null && object.flag === true;
}

let before;
for (let i = 0; i < 32; ++i) {
    before = createRetained();
    createHeapRetained(i);
}
ArkTools.arkSteedCompileAsync(createRetained);
ArkTools.waitJitCompileFinish(createRetained);
ArkTools.arkSteedCompileAsync(createHeapRetained);
ArkTools.waitJitCompileFinish(createHeapRetained);
print("compiled:" + (ArkTools.arkSteedIsCompiled(createRetained) &&
    ArkTools.arkSteedIsCompiled(createHeapRetained)));

// Move collectible roots after installation but before executing the new code.
ArkTools.forceFullGC();
let first = createRetained();
let firstHeap = createHeapRetained(99);
print("template-roots-after-full-gc:" + (first !== before && hasValues(first) && hasValues(before) &&
    firstHeap.text === "retained-heap-template" && firstHeap.read() === 99));

let survivors = [];
let heapSurvivors = [];
let previous = first;
let ok = true;
for (let i = 0; i < 4096; ++i) {
    let current = createRetained();
    let heapCurrent = createHeapRetained(i);
    if (!hasValues(current) || current === previous || heapCurrent.text !== "retained-heap-template" ||
        heapCurrent.read() !== i) {
        ok = false;
        break;
    }
    if (i % 256 === 0) {
        current.integer = { index: i };
        survivors.push(current);
        heapSurvivors.push(heapCurrent);
    }
    previous = current;
}

function checkSurvivors() {
    if (!ok || survivors.length !== 16 || heapSurvivors.length !== 16) {
        return false;
    }
    for (let i = 0; i < survivors.length; ++i) {
        let object = survivors[i];
        if (object.integer.index !== i * 256 || object.fraction !== 2.5 || object.empty !== null ||
            object.flag !== true || heapSurvivors[i].text !== "retained-heap-template" ||
            heapSurvivors[i].read() !== i * 256 || Object.getPrototypeOf(object) !== Object.prototype) {
            return false;
        }
    }
    return true;
}

// These APIs synchronously request the named collections; do not silently
// substitute a full GC for the young-GC check.
ArkTools.GC.startGC("young", undefined, true);
print("young-gc-survivors:" + checkSurvivors());
ArkTools.forceFullGC();
print("full-gc-survivors:" + checkSurvivors());

// Stores into retained, potentially promoted instances must keep fresh roots alive.
for (let i = 0; i < survivors.length; ++i) {
    survivors[i].integer = { index: i * 256 };
}
ArkTools.GC.startGC("young", undefined, true);
print("retained-object-stores:" + checkSurvivors());
let fresh = createRetained();
let freshHeap = createHeapRetained(123);
// Capacity failure may eagerly deopt createRetained; createHeapRetained uses the semantic fallback.
print("fresh-after-gc:" + (hasValues(fresh) && hasValues(before) && fresh !== first &&
    freshHeap.text === "retained-heap-template" && freshHeap.read() === 123 && firstHeap.read() === 99 &&
    ArkTools.arkSteedIsCompiled(createHeapRetained)));
