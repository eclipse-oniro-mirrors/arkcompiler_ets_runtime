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
//! DEOPT_COUNT *Deoptimize* 0

// Heap primitives return false directly instead of deoptimizing the walk.
//! METHOD      check_heap_primitive
//! HAS         BranchIfReferenceEqual
//! HAS         HasInPrototypeChain
//! HAS         CallCommonStub InstanceofJit

class PrimitiveHeapTarget {}

function check_heap_primitive(value: Object): number {
    return value instanceof PrimitiveHeapTarget ? 1 : 0;
}

for (let i = 0; i < 50; i++) {
    check_heap_primitive(new PrimitiveHeapTarget());
}

ArkTools.arkSteedCompileSync(check_heap_primitive);

print("string=" + check_heap_primitive("x" as Object));
print("symbol=" + check_heap_primitive(Symbol("s") as Object));
print("bigInt=" + check_heap_primitive(1n as Object));
print("number=" + check_heap_primitive(5 as Object));
print("hit=" + check_heap_primitive(new PrimitiveHeapTarget()));
