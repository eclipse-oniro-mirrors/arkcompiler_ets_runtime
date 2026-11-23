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

// check_rebind_runtime is compiled with the constant-target form. Rebinding its
// prototype changes ProtoOrHClass and routes only this site through InstanceofJit.
//! METHOD      check_rebind_runtime
//! HAS         BranchIfReferenceEqual
//! HAS         CallCommonStub InstanceofJit
//! DEOPT_COUNT check_rebind_runtime 0
//! DEOPT_COUNT check_array 0

// check_poly is compiled with the first target. A later target mismatch writes
// Hole without deoptimizing the installed code, and keeps the generic fallback.
//! METHOD      check_poly
//! HAS         BranchIfReferenceEqual
//! HAS         CallCommonStub InstanceofJit
//! HAS_NOT     DeoptIfHClassMismatch
//! DEOPT_COUNT check_poly 0

// Once a different target is observed before compilation, the slot is Hole and
// later compilation uses the generic path instead of a constant-target guard.
//! METHOD      check_poly_generic
//! COUNT       BranchIfReferenceEqual 1  # Only the boolean result conversion, not a target guard.
//! HAS_NOT     HasInPrototypeChain
//! COUNT       CallCommonStub InstanceofJit 1
//! HAS_NOT     DeoptIfHClassMismatch
//! DEOPT_COUNT check_poly_generic 0

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

function check_poly_generic(o: Object, t: Object): number {
    return o instanceof t ? 1 : 0;
}

let arr = [1, 2, 3];
for (let i = 0; i < 50; i++) {
    check_array(arr);
    check_rebind_runtime(new RebindV2(i));
    check_poly(new RebindV2(i), RebindV2);
    check_poly_generic(new RebindV2(i), RebindV2);
}
// A different constructor marks the slot Hole before compilation.
for (let i = 0; i < 50; i++) {
    check_poly_generic(new OtherV2(i), OtherV2);
}

ArkTools.arkSteedCompileSync(check_array);
ArkTools.arkSteedCompileSync(check_rebind_runtime);
ArkTools.arkSteedCompileSync(check_poly);
ArkTools.arkSteedCompileSync(check_poly_generic);
const boundOtherV2 = OtherV2.bind(null);

// Prototype rebinding must preserve semantics without invalidating other sites.
print("arrayHit=" + check_array(arr));
print("arrayMiss=" + check_array({}));
print("rebindBefore=" + check_rebind_runtime(new RebindV2(7)));

// Execute all constant-target and mismatch paths before the prototype rebind.
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
print("polyGeneric=" + check_poly_generic(new OtherV2(5), OtherV2));
print("arrayAfter=" + check_array(arr));
