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

//! COMPILE_COUNT_GE *storeUnseenCOW* 1
//! COMPILE_COUNT_GE *storeUpgradeMono* 1
//! COMPILE_COUNT_GE *storeUpgradePoly* 1
//! EXEC_COUNT_GE *storeUnseenCOW* 1
//! EXEC_COUNT_GE *storeUpgradeMono* 1
//! EXEC_COUNT_GE *storeUpgradePoly* 1
//! DEOPT_COUNT_GE *Deoptimize* 1

//! PARAMS --compiler-jit-hotness-threshold=60000 --enable-force-gc=true

function literal() {
    return ["first", "second"];
}

function writable() {
    const array = literal();
    array[0] = "warm";
    return array;
}

//! METHOD storeUnseenCOW
//! HAS DeoptIfCOWElements
//! HAS_NOT EnsureWritableElements
//! HAS StoreTaggedElementWithBarrier
//! HAS_NOT CallCommonStub SetPropertyByValue
//! CODEGEN_COUNT CopyCOWElements deferred call 0
function storeUnseenCOW(array, value) {
    array[0] = value;
    return array[0];
}

const warm = writable();
for (let i = 0; i < 20; ++i) {
    storeUnseenCOW(warm, "warm");
}
ArkTools.arkSteedCompileSync(storeUnseenCOW);
ArkTools.waitJitCompileFinish(storeUnseenCOW);
print(storeUnseenCOW(warm, "compiled") === "compiled" && ArkTools.arkSteedIsCompiled(storeUnseenCOW));
const lateCOW = literal();
const sibling = literal();
print(storeUnseenCOW(lateCOW, "changed") === "changed" && sibling[0] === "first");
print(ArkTools.isAOTDeoptimized(storeUnseenCOW));

// A later COW hit in a mono IC keeps the existing feedback. Only ArkSteed
// interprets that feedback, so compiled code must guard an unobserved COW backing.
//! METHOD storeUpgradeMono
//! HAS_NOT EnsureWritableElements
//! HAS DeoptIfCOWElements
//! HAS_NOT CallCommonStub SetPropertyByValue
//! CODEGEN_COUNT CopyCOWElements deferred call 0
function storeUpgradeMono(array, value) {
    array[0] = value;
    return array[0];
}

for (let i = 0; i < 20; ++i) {
    storeUpgradeMono(warm, "warm");
}
storeUpgradeMono(literal(), "cow");
storeUpgradeMono(warm, "warm");
ArkTools.arkSteedCompileSync(storeUpgradeMono);
ArkTools.waitJitCompileFinish(storeUpgradeMono);
const monoCOW = literal();
ArkTools.forceFullGC();
print(storeUpgradeMono(monoCOW, "mono") === "mono" && sibling[0] === "first" &&
      ArkTools.isAOTDeoptimized(storeUpgradeMono));

// A later COW hit in a poly IC also keeps its handler and requires a JIT guard.
//! METHOD storeUpgradePoly
//! HAS_NOT EnsureWritableElements
//! HAS DeoptIfCOWElements
//! HAS_NOT CallCommonStub SetPropertyByValue
//! CODEGEN_COUNT CopyCOWElements deferred call 0
function storeUpgradePoly(array, value) {
    array[0] = value;
    return array[0];
}

const otherShape = [1, "second"];
otherShape[0] = "warm";
for (let i = 0; i < 20; ++i) {
    storeUpgradePoly(warm, "warm");
    storeUpgradePoly(otherShape, "warm");
}
storeUpgradePoly(literal(), "cow");
ArkTools.arkSteedCompileSync(storeUpgradePoly);
ArkTools.waitJitCompileFinish(storeUpgradePoly);
const polyCOW = literal();
ArkTools.forceFullGC();
print(storeUpgradePoly(polyCOW, "poly") === "poly" && storeUpgradePoly(otherShape, "other") === "other" &&
      sibling[0] === "first" && ArkTools.isAOTDeoptimized(storeUpgradePoly));
