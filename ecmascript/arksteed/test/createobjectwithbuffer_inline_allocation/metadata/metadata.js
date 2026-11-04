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

// Compile before any clone mutation; use explicit ordinary collections below.
//! PARAMS --asm-interpreter=true --compiler-jit-hotness-threshold=60000 --compiler-enable-baselinejit=false --enable-cmc-gc=false

//! METHOD createMetadata
//! COUNT AllocateInYoung 1
//! BYTECODE_COUNT createobjectwithbuffer 0x0,* 1
//! HAS HeapConstant
//! COUNT InitializeObjectWithBuffer 1
//! HAS_NOT CallCommonStub CreateObjectHavingMethod
//! CODEGEN_COUNT * CheckAllocationSize * 0
//! CODEGEN_COUNT * InitializeObjectWithBuffer * 1
//! CODEGEN_COUNT AllocateInYoung bump allocation 1

// Compute -0 outside the target so integer negation does not repeatedly deopt it.
const negativeZero = -0;
function createMetadata() {
    // Runtime profiling chooses representations; negativeZero is a post-clone store.
    // The native test supplies explicit raw +0/-0/subnormal/INT template slots.
    return { zero: 0, negativeZero: negativeZero, tiny: 5e-324, fraction: 1.25,
        integer: 42, flag: true, empty: null };
}

const keys = ["zero", "negativeZero", "tiny", "fraction", "integer", "flag", "empty"];
const values = [0, -0, 5e-324, 1.25, 42, true, null];
function pristine(object) {
    if (Object.keys(object).join(",") !== keys.join(",") ||
        Object.getPrototypeOf(object) !== Object.prototype || !Object.isExtensible(object)) {
        return false;
    }
    for (let i = 0; i < keys.length; ++i) {
        const descriptor = Object.getOwnPropertyDescriptor(object, keys[i]);
        if (descriptor === undefined || !("value" in descriptor) ||
            !Object.is(descriptor.value, values[i]) || !descriptor.writable ||
            !descriptor.enumerable || !descriptor.configurable) {
            return false;
        }
    }
    return true;
}

let before;
for (let i = 0; i < 32; ++i) {
    before = createMetadata();
}
print("interpreted:" + (!ArkTools.arkSteedIsCompiled(createMetadata) && pristine(before)));
ArkTools.arkSteedCompileAsync(createMetadata);
ArkTools.waitJitCompileFinish(createMetadata);
print("compiled:" + ArkTools.arkSteedIsCompiled(createMetadata));

// Counts above only establish generated code shape, not runtime slow-path hits.
const typeClone = createMetadata();
const addedClone = createMetadata();
const descriptorClone = createMetadata();
const protoClone = createMetadata();
const sibling = createMetadata();
print("initial-clones:" + ([typeClone, addedClone, descriptorClone, protoClone, sibling].every(pristine) &&
    new Set([before, typeClone, addedClone, descriptorClone, protoClone, sibling]).size === 6));

// Clone isolation must not depend on installed code.
function freshIsolated() {
    const fresh = createMetadata();
    return pristine(fresh) && pristine(before) && pristine(sibling) &&
        fresh !== before && fresh !== sibling && fresh !== typeClone && fresh !== addedClone &&
        fresh !== descriptorClone && fresh !== protoClone;
}

typeClone.zero = { marker: 10 };
typeClone.negativeZero = true;
typeClone.tiny = null;
typeClone.fraction = { marker: 20 };
typeClone.integer = 3.5;
print("type-isolation:" + freshIsolated());

// Grow one clone beyond inline capacity without changing the cached literal.
for (let i = 0; i < 32; ++i) {
    addedClone["extra" + i] = i;
}
print("property-isolation:" + (Object.keys(addedClone).length >
    ArkTools.getInlinedPropertiesCount(addedClone) && freshIsolated()));

Object.defineProperty(descriptorClone, "fraction", {
    get() { return 99; }, enumerable: false, configurable: false
});
Object.defineProperty(descriptorClone, "flag", { value: false, writable: false });
print("descriptor-isolation:" + freshIsolated());

Object.setPrototypeOf(protoClone, { inherited: 71 });
print("prototype-isolation:" + freshIsolated());

function mutationsRetained() {
    const descriptor = Object.getOwnPropertyDescriptor(descriptorClone, "fraction");
    let addedOk = Object.keys(addedClone).length === keys.length + 32;
    for (let i = 0; i < 32; ++i) {
        addedOk = addedOk && addedClone["extra" + i] === i;
    }
    return typeClone.zero.marker === 10 && typeClone.negativeZero === true && typeClone.tiny === null &&
        typeClone.fraction.marker === 20 && typeClone.integer === 3.5 && addedOk &&
        descriptorClone.fraction === 99 && typeof descriptor.get === "function" &&
        !descriptor.enumerable && !descriptor.configurable && descriptorClone.flag === false &&
        !Object.getOwnPropertyDescriptor(descriptorClone, "flag").writable &&
        protoClone.inherited === 71 && Object.getPrototypeOf(protoClone) !== Object.prototype;
}

ArkTools.GC.startGC("young", undefined, true);
print("young-gc-isolation:" + (mutationsRetained() && freshIsolated()));
ArkTools.forceFullGC();
print("full-gc-isolation:" + (mutationsRetained() && freshIsolated()));
