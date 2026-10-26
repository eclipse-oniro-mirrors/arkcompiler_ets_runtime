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

//! PARAMS --asm-interpreter=true --compiler-jit-hotness-threshold=60000 --compiler-jit-call-threshold=60000 --compiler-enable-baselinejit=false --enable-cmc-gc=false
//! DEOPT_COUNT * 0

// These constraints prove code shape, not execution of the capacity-failure edge.
// Synchronous reentry and GC recovery are outside this test contract.
// Loop peeling emits one first-iteration copy and one loop-body copy of each create bytecode.

//! METHOD emptyObjectBurst
//! BYTECODE_COUNT createemptyobject 1
//! COUNT AllocateInYoung 2
//! COUNT CallRuntime AllocateInYoung 2
//! COUNT CanAllocateInYoung 2
//! COUNT_GE BranchIfTrue 2
//! COUNT InitializeEmptyObject 2
//! HAS_NOT CallCommonStub NewJSObject
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 2

function emptyObjectBurst(count, root, seed) {
    const integer = (seed + 17) | 0;
    const fraction = seed * 1.25 + 0.5;
    let result = root;
    let completed = 0;
    try {
        for (let i = 0; i < count; ++i) {
            const previous = result;
            result = {};
            if (result === previous) {
                throw root;
            }
            completed++;
        }
    } catch (error) {
        // Keep the original root available at the create bytecode's exception continuation.
        if (error !== root) {
            throw root;
        }
        throw error;
    }
    if (completed !== count || root.token !== 91 || integer !== 24 || fraction !== 9.25) {
        throw root;
    }
    return result;
}

//! METHOD bufferedObjectBurst
//! BYTECODE_COUNT createobjectwithbuffer 1
//! COUNT AllocateInYoung 2
//! COUNT CallRuntime AllocateInYoung 2
//! COUNT CanAllocateInYoung 2
//! COUNT_GE BranchIfTrue 2
//! COUNT InitializeObjectWithBuffer 2
//! HAS_NOT CallCommonStub CreateObjectHavingMethod
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 2

function bufferedObjectBurst(count, root, seed) {
    const integer = (seed + 17) | 0;
    const fraction = seed * 1.25 + 0.5;
    let result = root;
    let completed = 0;
    try {
        for (let i = 0; i < count; ++i) {
            const previous = result;
            result = {a: 11, b: 2.5, c: true};
            if (result === previous) {
                throw root;
            }
            completed++;
        }
    } catch (error) {
        // Keep the original root available at the create bytecode's exception continuation.
        if (error !== root) {
            throw root;
        }
        throw error;
    }
    if (completed !== count || root.token !== 91 || integer !== 24 || fraction !== 9.25) {
        throw root;
    }
    return result;
}

//! METHOD emptyArrayBurst
//! CODEGEN_COUNT * PrepareEmptyArray * 0
//! CODEGEN_COUNT * CheckYoungAllocation * 0
//! CODEGEN_COUNT * LoadObjectSize * 0
//! CODEGEN_COUNT * CheckAllocationSize * 0
//! CODEGEN_COUNT * CheckArrayInitialization * 0
//! BYTECODE_COUNT createemptyarray 1
//! COUNT AllocateInYoung 2
//! COUNT CallRuntime AllocateInYoung 2
//! COUNT CanAllocateInYoung 2
//! COUNT_GE BranchIfTrue 2
//! COUNT InitializeEmptyArray 2
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 2

function emptyArrayBurst(count, root, seed) {
    const integer = (seed + 17) | 0;
    const fraction = seed * 1.25 + 0.5;
    let result = root;
    let completed = 0;
    try {
        for (let i = 0; i < count; ++i) {
            const previous = result;
            result = [];
            if (result === previous) {
                throw root;
            }
            completed++;
        }
    } catch (error) {
        // Keep the original root available at the create bytecode's exception continuation.
        if (error !== root) {
            throw root;
        }
        throw error;
    }
    if (completed !== count || root.token !== 91 || integer !== 24 || fraction !== 9.25) {
        throw root;
    }
    return result;
}

//! METHOD bufferedArrayBurst
//! BYTECODE_COUNT createarraywithbuffer 1
//! COUNT AllocateInYoung 2
//! COUNT CallRuntime AllocateInYoung 2
//! COUNT CanAllocateInYoung 2
//! COUNT_GE BranchIfTrue 2
//! COUNT InitializeArrayWithBuffer 2
//! HAS_NOT CallCommonStub CreateArrayWithBuffer
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 2

function bufferedArrayBurst(count, root, seed) {
    const integer = (seed + 17) | 0;
    const fraction = seed * 1.25 + 0.5;
    let result = root;
    let completed = 0;
    try {
        for (let i = 0; i < count; ++i) {
            const previous = result;
            result = [11, 22, 33];
            if (result === previous) {
                throw root;
            }
            completed++;
        }
    } catch (error) {
        // Keep the original root available at the create bytecode's exception continuation.
        if (error !== root) {
            throw root;
        }
        throw error;
    }
    if (completed !== count || root.token !== 91 || integer !== 24 || fraction !== 9.25) {
        throw root;
    }
    return result;
}

const root = {token: 91};
const creators = [emptyObjectBurst, bufferedObjectBurst, emptyArrayBurst, bufferedArrayBurst];
for (const create of creators) {
    for (let i = 0; i < 30; ++i) {
        create(2, root, 7);
    }
    ArkTools.arkSteedCompileSync(create);
    if (!ArkTools.arkSteedIsCompiled(create)) {
        throw new Error("creator did not compile");
    }
}
const emptyObject = emptyObjectBurst(100000, root, 7);
if (!ArkTools.arkSteedIsCompiled(emptyObjectBurst)) {
    throw new Error("empty object capacity pressure discarded compiled code");
}
const bufferedObject = bufferedObjectBurst(100000, root, 7);
const emptyArray = emptyArrayBurst(100000, root, 7);
const bufferedArray = bufferedArrayBurst(100000, root, 7);
for (const create of creators) {
    if (!ArkTools.arkSteedIsCompiled(create)) {
        throw new Error("capacity pressure discarded compiled code");
    }
}
print("empty-object:" + (Object.getPrototypeOf(emptyObject) === Object.prototype &&
    Object.keys(emptyObject).length === 0));
print("buffered-object:" + (bufferedObject.a === 11 && bufferedObject.b === 2.5 && bufferedObject.c === true));
print("empty-array:" + (Array.isArray(emptyArray) && emptyArray.length === 0));
print("buffered-array:" + (Array.isArray(bufferedArray) && bufferedArray.length === 3 &&
    bufferedArray[0] === 11 && bufferedArray[1] === 22 && bufferedArray[2] === 33));
