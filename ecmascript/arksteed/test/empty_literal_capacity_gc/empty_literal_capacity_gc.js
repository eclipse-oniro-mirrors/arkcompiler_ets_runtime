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

//! PARAMS --asm-interpreter=true --compiler-jit-hotness-threshold=60000 --compiler-jit-call-threshold=60000 --compiler-enable-baselinejit=false --enable-cmc-gc=false --enable-force-gc=true
//! METHOD createObject
//! BYTECODE_COUNT createemptyobject 1
//! HAS_NOT CallRuntime CreateEmptyObject
//! COUNT AllocateInYoung 1
//! COUNT CallRuntime AllocateInYoung 1
//! COUNT CanAllocateInYoung 1
//! COUNT_GE BranchIfTrue 1
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 1
//! COUNT InitializeEmptyObject 1

function createObject(root, number) {
    const result = {};
    // Both a heap reference and an untagged value must survive the slow call.
    if (root.token !== 91 || number * 1.25 !== 8.75) {
        throw new Error("live value corrupted");
    }
    return result;
}

//! METHOD createArray
//! BYTECODE_COUNT createemptyarray 1
//! HAS_NOT CallRuntime CreateEmptyArray
//! COUNT AllocateInYoung 1
//! COUNT CallRuntime AllocateInYoung 1
//! COUNT CanAllocateInYoung 1
//! COUNT_GE BranchIfTrue 1
//! CODEGEN_COUNT *CallRuntime*RTStub_AllocateInYoung* 1
//! COUNT InitializeEmptyArray 1

function createArray(root, number) {
    const result = [];
    // Both a heap reference and an untagged value must survive the slow call.
    if (root.token !== 91 || number * 1.25 !== 8.75) {
        throw new Error("live value corrupted");
    }
    return result;
}

const root = {token: 91};
for (let i = 0; i < 30; i++) {
    createObject(root, 7);
    createArray(root, 7);
}
ArkTools.arkSteedCompileSync(createObject);
ArkTools.arkSteedCompileSync(createArray);
if (!ArkTools.arkSteedIsCompiled(createObject) || !ArkTools.arkSteedIsCompiled(createArray)) {
    throw new Error("creator did not compile");
}
const objects = [];
const arrays = [];
for (let i = 0; i < 1024; i++) {
    const object = createObject(root, 7);
    if (Object.getOwnPropertyNames(object).length !== 0 || Object.getPrototypeOf(object) !== Object.prototype) {
        throw new Error("incorrect empty object");
    }
    object.value = i;
    objects.push(object);

    const array = createArray(root, 7);
    if (!Array.isArray(array) || array.length !== 0 || Object.getPrototypeOf(array) !== Array.prototype) {
        throw new Error("incorrect empty array");
    }
    array.push(i, root);
    arrays.push(array);
}
ArkTools.GC.startGC("young", undefined, true);
ArkTools.forceFullGC();
for (let i = 0; i < objects.length; i++) {
    if (objects[i].value !== i || (i > 0 && objects[i] === objects[i - 1])) {
        throw new Error("retained object corrupted");
    }
    const array = arrays[i];
    if (array.length !== 2 || array[0] !== i || array[1] !== root ||
        (i > 0 && array === arrays[i - 1])) {
        throw new Error("retained array corrupted");
    }
}
if (!ArkTools.arkSteedIsCompiled(createObject) || !ArkTools.arkSteedIsCompiled(createArray)) {
    throw new Error("capacity pressure discarded compiled code");
}
print("empty-object-gc:ok");
print("empty-array-gc:ok");
