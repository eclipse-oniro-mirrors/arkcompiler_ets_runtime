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

// check_array: the target is a global reference (Array). The constructor-identity
// INSTANCEOF IC records the constructor object, so this site compiles to the
// constant-target form: identity branch + embedded ctorProto (no prototype-slot guards).
//! METHOD      check_array
//! HAS         BranchIfReferenceEqual
//! HAS         HasInPrototypeChain
//! HAS_NOT     CallCommonStub OrdinaryHasInstance
//! HAS_NOT     DeoptIfHClassMismatch
//! HAS         CallCommonStub InstanceofJit

// check_rebind_runtime is compiled with the constant-target form; the test then
// reassigns RebindV2.prototype, flipping FunctionPrototypeChangedDetector: the
// compiled code is lazily deoptimized and later executions observe the new
// prototype through the interpreter's generic resolution.
//! METHOD      check_rebind_runtime
//! HAS         BranchIfReferenceEqual
//! HAS         CallCommonStub InstanceofJit

// check_poly keeps the first recorded target and specializes on it. An identity
// mismatch branches directly to InstanceofJit for full semantics instead of
// repeatedly deoptimizing back to the interpreter.
//! METHOD      check_poly
//! HAS         BranchIfReferenceEqual
//! HAS         CallCommonStub InstanceofJit
//! HAS_NOT     DeoptIfHClassMismatch
//! DEOPT_COUNT check_poly 0

class OtherV2 {
    x: number;
    constructor(x: number) { this.x = x; }
}

class CustomV2 {}
Object.defineProperty(CustomV2, Symbol.hasInstance, {
    value: function (v: Object): boolean { return true; }
});

// Plain function (class constructors have a non-writable "prototype").
function RebindV2(this: any, x: number) {
    this.x = x;
}

function check_array(v: Object): number {
    return v instanceof Array ? 1 : 0;
}

function check_rebind_runtime(o: Object): number {
    return o instanceof RebindV2 ? o.x : 0;
}

function check_poly(o: Object, t: Object): number {
    return o instanceof t ? 1 : 0;
}

let arr = [1, 2, 3];
for (let i = 0; i < 50; i++) {
    check_array(arr);
    check_rebind_runtime(new RebindV2(i));
    check_poly(new RebindV2(i), RebindV2);
}
// Second constructor at the same site: the slot keeps the first target (no
// rewrite for a different constructor); executions fall back to generic
// resolution while the recorded feedback stays monomorphic.
for (let i = 0; i < 50; i++) {
    check_poly(new OtherV2(i), OtherV2);
}

ArkTools.arkSteedCompileSync(check_array);
ArkTools.arkSteedCompileSync(check_rebind_runtime);
ArkTools.arkSteedCompileSync(check_poly);
const boundOtherV2 = OtherV2.bind(null);

// Runtime invalidation: F.prototype = x flips the detector and lazily deopts the
// code compiled above; results must stay correct afterwards.
print("arrayHit=" + check_array(arr));
print("arrayMiss=" + check_array({}));
print("rebindBefore=" + check_rebind_runtime(new RebindV2(7)));

// Execute all constant-target and mismatch paths before flipping the detector.
const polyResult = check_poly(new RebindV2(1), RebindV2);
const polyOtherResult = check_poly(new OtherV2(2), OtherV2);
const polyCustomResult = check_poly(new CustomV2(), CustomV2);
const polyBoundResult = check_poly(new OtherV2(4), boundOtherV2);

RebindV2.prototype = { x: 1 };

print("rebindAfter=" + check_rebind_runtime(new RebindV2(9)));
print("rebindOld=" + check_rebind_runtime(new OtherV2(3)));
print("poly=" + polyResult);
print("polyOther=" + polyOtherResult);
print("polyCustom=" + polyCustomResult);
print("polyBound=" + polyBoundResult);
print("arrayAfter=" + check_array(arr));
