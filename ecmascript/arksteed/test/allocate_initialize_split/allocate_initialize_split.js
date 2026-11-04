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

// Collect profiles before explicitly compiling the four allocation sites.
//! PARAMS --asm-interpreter=true --compiler-jit-hotness-threshold=60000 --compiler-enable-baselinejit=false --enable-cmc-gc=false

//! METHOD createAll
//! BYTECODE_COUNT createemptyobject 1
//! BYTECODE_COUNT createobjectwithbuffer 1
//! BYTECODE_COUNT createemptyarray 1
//! BYTECODE_COUNT createarraywithbuffer 1
//! COUNT AllocateInYoung 4
//! COUNT CallRuntime AllocateInYoung 4
//! COUNT CanAllocateInYoung 4
//! COUNT BranchIfTrue 4
//! COUNT InitializeEmptyObject 1
//! COUNT InitializeObjectWithBuffer 1
//! COUNT InitializeEmptyArray 1
//! COUNT InitializeArrayWithBuffer 1
//! CODEGEN_COUNT * PrepareEmptyArray * 0
//! CODEGEN_COUNT * CheckYoungAllocation * 0
//! CODEGEN_COUNT * CheckAllocationSize * 0
//! CODEGEN_COUNT * CheckArrayInitialization * 0
//! CODEGEN_COUNT * CheckArrayLiteral * 0
//! CODEGEN_COUNT * LoadObjectSize * 0
//! HAS HeapConstant
//! HAS_NOT LoadInt32Field
//! HAS_NOT CallCommonStub NewJSObject
//! HAS_NOT CallCommonStub CreateObjectHavingMethod
//! HAS_NOT CallCommonStub CreateEmptyArray
//! HAS_NOT CallCommonStub CreateArrayWithBuffer
//! CODEGEN_COUNT AllocateInYoung bump allocation 4
//! CODEGEN_COUNT * InitializeEmptyObject * 1
//! CODEGEN_COUNT * InitializeObjectWithBuffer * 1
//! CODEGEN_COUNT * InitializeEmptyArray * 1
//! CODEGEN_COUNT * InitializeArrayWithBuffer * 1
//! CODEGEN_COUNT *fallback sentinel* 0

function createAll(root) {
    const empty = {};
    const object = { integer: 17, fraction: 2.5, flag: true, empty: null };
    const array = [];
    const literal = [11, 22, 33];
    // Completed results and the caller's root stay live across subsequent allocations.
    empty.object = object;
    empty.array = array;
    empty.literal = literal;
    empty.root = root;
    return empty;
}

function pristine(value, root) {
    const descriptor = Object.getOwnPropertyDescriptor(value.array, "length");
    return Object.getPrototypeOf(value) === Object.prototype &&
        Object.getPrototypeOf(value.object) === Object.prototype &&
        value.object.integer === 17 && value.object.fraction === 2.5 &&
        value.object.flag === true && value.object.empty === null &&
        Array.isArray(value.array) && Object.getPrototypeOf(value.array) === Array.prototype &&
        descriptor.value === 0 && descriptor.writable && !descriptor.enumerable && !descriptor.configurable &&
        !(0 in value.array) && Array.isArray(value.literal) &&
        value.literal.length === 3 && value.literal[0] === 11 &&
        value.literal[1] === 22 && value.literal[2] === 33 && value.root === root;
}

const root = { marker: 99 };
let before;
for (let i = 0; i < 32; ++i) {
    before = createAll(root);
}
print("interpreted:" + (!ArkTools.arkSteedIsCompiled(createAll) && pristine(before, root)));
ArkTools.arkSteedCompileAsync(createAll);
ArkTools.waitJitCompileFinish(createAll);
print("compiled:" + ArkTools.arkSteedIsCompiled(createAll));

// Move protected roots after code installation; this does not prove slow-path entry.
ArkTools.forceFullGC();
let first = createAll(root);
let second = createAll(root);
print("split-results:" + (pristine(first, root) && pristine(second, root) && pristine(before, root) &&
    first !== second && first !== before && first.object !== second.object &&
    first.array !== second.array && first.literal !== second.literal));
first.object.integer = root;
first.array.push(root);
first.literal[0] = root;
second.literal.length = 1;
print("independent-and-cow:" + (first.object.integer === root && first.array[0] === root &&
    first.literal[0] === root && first.literal.length === 3 && second.object.integer === 17 &&
    second.array.length === 0 && second.literal.length === 1 && second.literal[0] === 11 &&
    pristine(before, root) && pristine(createAll(root), root)));

const iterations = 60000;
const stride = 3000;
let survivors = [];
let previous = before;
let ok = true;
// Natural pressure also works in release; it does not by itself prove an internal GC hit.
for (let i = 0; i < iterations; ++i) {
    let current = createAll(root);
    if (!pristine(current, root) || current === previous || current.object === previous.object ||
        current.array === previous.array || current.literal === previous.literal) {
        ok = false;
        break;
    }
    if (i % stride === 0) {
        survivors.push(current);
    }
    previous = current;
}
// Capacity exhaustion is handled inside allocation; results must remain valid.
print("allocation-pressure:" + (ok && survivors.length === iterations / stride));

function retained() {
    return survivors.length === iterations / stride && survivors.every(value => pristine(value, root)) &&
        first.object.integer === root && first.array[0] === root && first.literal[0] === root &&
        root.marker === 99 && pristine(createAll(root), root);
}
ArkTools.GC.startGC("young", undefined, true);
print("young-gc:" + retained());
ArkTools.forceFullGC();
print("full-gc:" + retained());
