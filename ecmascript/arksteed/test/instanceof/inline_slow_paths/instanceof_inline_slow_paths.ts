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

// check_deep / check_proxy use the constant-target form: identity mismatch goes
// to InstanceofJit, while the matching target uses one loop-based chain-walk node.
//! METHOD      check_deep
//! HAS         BranchIfReferenceEqual
//! HAS         HasInPrototypeChain
//! HAS_NOT     CallCommonStub OrdinaryHasInstance
//! HAS         CallCommonStub InstanceofJit

//! METHOD      check_proxy
//! HAS         BranchIfReferenceEqual
//! HAS         HasInPrototypeChain
//! HAS_NOT     CallCommonStub OrdinaryHasInstance
//! HAS         CallCommonStub InstanceofJit

// InstanceofSlowReplaced.prototype is reassigned before this compile, flipping
// FunctionPrototypeChangedDetector. Without a constant target the whole site
// falls back to the full-semantics InstanceofJit stub.
//! METHOD      check_replaced_proto
//! HAS_NOT     DeoptIfHClassMismatch
//! HAS_NOT     HasInPrototypeChain
//! HAS_NOT     CallCommonStub OrdinaryHasInstance
//! HAS         CallCommonStub InstanceofJit

// An arrow function has no instance prototype, so it also uses the generic stub.
//! METHOD      check_arrow_target
//! HAS_NOT     DeoptIfHClassMismatch
//! HAS_NOT     HasInPrototypeChain
//! HAS_NOT     CallCommonStub OrdinaryHasInstance
//! HAS         CallCommonStub InstanceofJit

class InstanceofSlowBase {}
class InstanceofSlowL1 extends InstanceofSlowBase {}
class InstanceofSlowL2 extends InstanceofSlowL1 {}
class InstanceofSlowL3 extends InstanceofSlowL2 {}
class InstanceofSlowL4 extends InstanceofSlowL3 {}
class InstanceofSlowL5 extends InstanceofSlowL4 {}
class InstanceofSlowL6 extends InstanceofSlowL5 {}
class InstanceofSlowL7 extends InstanceofSlowL6 {}
class InstanceofSlowL8 extends InstanceofSlowL7 {}
class InstanceofSlowDeep extends InstanceofSlowL8 {}
class InstanceofSlowPoint extends InstanceofSlowL8 {}

class InstanceofSlowProxyTarget {}

function InstanceofSlowReplaced(this: any) {}

function check_deep(o: Object): number {
    return o instanceof InstanceofSlowPoint ? 1 : 0;
}

function check_proxy(o: Object): number {
    return o instanceof InstanceofSlowProxyTarget ? 1 : 0;
}

function check_replaced_proto(o: Object): number {
    return o instanceof InstanceofSlowReplaced ? 1 : 0;
}

function check_arrow_target(o: Object): number {
    try {
        return o instanceof arrowTarget ? 1 : 0;
    } catch (e) {
        return 2;
    }
}

const arrowTarget = (a: number) => a + 1;

let proxied: Object = new Proxy({}, {});

// Warmup: populate each instanceof IC slot with its mono target.
for (let i = 0; i < 50; i++) {
    check_deep(new InstanceofSlowDeep());
    check_proxy(proxied);
    check_replaced_proto(new InstanceofSlowReplaced());
    check_arrow_target(new Object());
}

// Compile while the detector is still intact: constant-target form.
ArkTools.arkSteedCompileSync(check_deep);
ArkTools.arkSteedCompileSync(check_proxy);
ArkTools.arkSteedCompileSync(check_arrow_target);

// Run the new chain-walk node before the detector flip deoptimizes this code.
const deepHitResult = check_deep(new InstanceofSlowPoint());
const deepMissResult = check_deep(new InstanceofSlowDeep());
const proxyResult = check_proxy(proxied);
const proxyHitResult = check_proxy(new InstanceofSlowProxyTarget());

// Replace the whole prototype after warmup: flips FunctionPrototypeChangedDetector
// (lazily deoptimizing the code compiled above) and disables the constant-target
// path for sites compiled from now on.
InstanceofSlowReplaced.prototype = { marker: 1 };

ArkTools.arkSteedCompileSync(check_replaced_proto);

print("deepHit=" + deepHitResult);
print("deepMiss=" + deepMissResult);
print("proxy=" + proxyResult);
print("proxyHit=" + proxyHitResult);
print("replacedNew=" + check_replaced_proto(new InstanceofSlowReplaced()));
print("arrowTarget=" + check_arrow_target(new Object()));
