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

//! PARAMS --compiler-jit-hotness-threshold=60000
//! METHOD makeEmptyObject
//! COMPILE_COUNT_GE *makeEmptyObject* 1
//! EXEC_COUNT_GE *makeEmptyObject* 1
//! DEOPT_COUNT *Deoptimize* 0
//! HAS_NOT BranchIfReferenceEqual
//! CODEGEN_COUNT * CheckYoungAllocation * 0
//! CODEGEN_COUNT * CheckAllocationSize * 0
//! COUNT AllocateInYoung 1
//! COUNT InitializeEmptyObject 1
//! HAS HeapConstant
//! HAS_NOT LoadTaggedField
//! HAS_NOT LoadI64FromAddress
//! HAS_NOT LoadTaggedFromAddress
//! HAS_NOT CallCommonStub NewJSObject
//! COUNT Phi 1
//! HAS_NOT BranchIfInt32Compare
//! HAS_NOT CallRuntime CreateEmptyObject
//! CODEGEN_COUNT AllocateInYoung bump allocation 1
//! CODEGEN_COUNT * InitializeEmptyObject * 1
//! CODEGEN_COUNT CreateEmptyObject fallback sentinel 0

function makeEmptyObject() {
    return {};
}

for (let i = 0; i < 20000; i++) {
    makeEmptyObject();
}

const intrinsicObject = Object;
const prototypeBeforeCompile = makeEmptyObject();
const derivedBeforeCompile = intrinsicObject.create(prototypeBeforeCompile);
prototypeBeforeCompile.marker = 7;
intrinsicObject.setPrototypeOf(prototypeBeforeCompile, null);

// Rebinding the global name must not change the intrinsic used for literals.
globalThis.Object = function ReplacementObject() {
    throw new Error("Object literals must not call the replacement constructor");
};
const interpreted = makeEmptyObject();
ArkTools.arkSteedCompileSync(makeEmptyObject);
ArkTools.waitJitCompileFinish(makeEmptyObject);

let ok = ArkTools.isAOTCompiled(makeEmptyObject);
globalThis.Object = null;
const first = makeEmptyObject();
const second = makeEmptyObject();
globalThis.Object = intrinsicObject;

ok = ok && Object.getPrototypeOf(interpreted) === Object.prototype;
ok = ok && Object.keys(interpreted).length === 0;
ok = ok && derivedBeforeCompile.marker === 7;

ok = ok && first !== second;
ok = ok && Object.getPrototypeOf(first) === Object.prototype;
ok = ok && Object.getPrototypeOf(second) === Object.prototype;
ok = ok && Object.keys(first).length === 0;
ok = ok && Object.keys(second).length === 0;
ok = ok && Object.isExtensible(first);
ok = ok && Object.isExtensible(second);

// Prototype transitions after compilation must also leave the initial layout intact.
const derived = Object.create(first);
Object.setPrototypeOf(second, null);
const fresh = makeEmptyObject();
ok = ok && Object.getPrototypeOf(fresh) === Object.prototype;
ok = ok && Object.keys(fresh).length === 0;
ok = ok && Object.isExtensible(fresh);
ok = ok && fresh !== first && fresh !== second;
ok = ok && ArkTools.isAOTCompiled(makeEmptyObject);

first.marker = 11;
ok = ok && first.marker === 11;
ok = ok && derived.marker === 11;
ok = ok && second.marker === undefined;
ok = ok && Object.keys(second).length === 0;

second.marker = 22;
ok = ok && first.marker === 11;
ok = ok && second.marker === 22;

first[3] = 33;
ok = ok && first[3] === 33;
ok = ok && second[3] === undefined;

const identities = new Map();
identities.set(first, 41);
identities.set(second, 42);
ok = ok && identities.get(first) === 41;
ok = ok && identities.get(second) === 42;
ok = ok && identities.size === 2;

print(ok ? "PASS" : "FAIL");
