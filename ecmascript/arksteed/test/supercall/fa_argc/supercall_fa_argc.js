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

/*
 * argc fidelity of the forwarded caller-frame copy. ActualArgc drives the
 * copy loop, so the base constructor's rest parameter must observe exactly
 * the count the caller pushed - for even and odd counts (both parities of
 * the aligned slot reservation) and through spread and Reflect.construct
 * callers that materialize the count before the construct.
 */

//! METHOD PassF
//! HAS         AllocateInYoung
//! HAS         CallBaseConstructorForwardAllArgs
//! HAS         ActualArgc
//! COUNT       CallRuntime OptSuperCall* 1

class SinkBase {
    constructor(...rest) {
        this.n = rest.length;
        this.joined = rest.join(",");
    }
}

class PassF extends SinkBase {
}

ArkTools.arkSteedCompileSync(SinkBase);
ArkTools.arkSteedCompileSync(PassF);

const a0 = new PassF();
print("argc0: " + a0.n + " '" + a0.joined + "'");

const a1 = new PassF(9);
print("argc1: " + a1.n + " '" + a1.joined + "'");

const a2 = new PassF(1, 2);
print("argc2: " + a2.n + " '" + a2.joined + "'");

const a5 = new PassF(1, 2, 3, 4, 5);
print("argc5: " + a5.n + " '" + a5.joined + "'");

const sp = new PassF(...[7, 8]);
print("spread: " + sp.n + " '" + sp.joined + "'");

const rc = Reflect.construct(PassF, [4, 5, 6]);
print("reflect: " + rc.n + " '" + rc.joined + "' " + (rc instanceof PassF));
