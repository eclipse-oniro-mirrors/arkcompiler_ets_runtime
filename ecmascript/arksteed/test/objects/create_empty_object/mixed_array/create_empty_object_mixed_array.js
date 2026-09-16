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

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-force-gc=true
//! METHOD makeObjectAndArray
//! COUNT CreateEmptyObject 1
//! COUNT CallCommonStub NewJSObject 1
//! COUNT AllocateJSArray 1
//! HAS_NOT CallRuntime CreateEmptyObject
//! BYTECODE_COUNT createemptyobject 1
//! BYTECODE_COUNT createemptyarray 1
//! CODEGEN_COUNT CreateEmptyObject fast bump allocation 1
//! CODEGEN_COUNT CreateEmptyObject fallback sentinel 1
//! CODEGEN_COUNT AllocateJSArray inline success 1
//! CODEGEN_COUNT AllocateJSArray fallback sentinel 1

function makeObjectAndArray() {
    const object = {};
    const array = [];
    object.array = array;
    array[0] = object;
    return object;
}

function checkObjectAndArray(object) {
    return Object.getPrototypeOf(object) === Object.prototype &&
        Object.keys(object).length === 1 &&
        Array.isArray(object.array) &&
        Object.getPrototypeOf(object.array) === Array.prototype &&
        object.array.length === 1 && object.array[0] === object;
}

const interpreted = makeObjectAndArray();
let ok = checkObjectAndArray(interpreted);
for (let i = 0; i < 20000; i++) {
    makeObjectAndArray();
}

ArkTools.arkSteedCompileSync(makeObjectAndArray);
ArkTools.waitJitCompileFinish(makeObjectAndArray);
ok = ok && ArkTools.isAOTCompiled(makeObjectAndArray);

const first = makeObjectAndArray();
const second = makeObjectAndArray();
ok = ok && checkObjectAndArray(first) && checkObjectAndArray(second);
ok = ok && first !== second && first.array !== second.array;
first.array.push(second);
ok = ok && first.array.length === 2 && first.array[1] === second;
ok = ok && second.array.length === 1 && interpreted.array.length === 1;

// Keep both allocation kinds reachable across a collection.
ArkTools.forceFullGC();
ok = ok && first.array[0] === first && first.array[1] === second;
ok = ok && checkObjectAndArray(second) && checkObjectAndArray(interpreted);
print(ok ? "PASS" : "FAIL");
