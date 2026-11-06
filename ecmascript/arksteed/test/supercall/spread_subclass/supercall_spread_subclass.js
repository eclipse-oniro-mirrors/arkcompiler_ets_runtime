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
 * Array-subclass spread operands. The interpreter's SUPERCALLSPREAD fast
 * path (interpreter_stub GetCallSpreadArgs circuit) admits a JSArray whose
 * elements kind is non-GENERIC without any hclass-identity check, relying on
 * the IsJSArrayPrototypeModified bit for prototype validation — and that
 * bit is not maintained for non-GENERIC kinds (IsProtoNotChangeJSArray
 * early-returns), so a subclass instance created through ArrayCreate and
 * kind-transitioned by its stores passes the admission with a custom
 * @@iterator still installed on the subclass prototype.
 *
 * NOTE: this matches the interpreter and the old JIT byte for byte (a plain
 * ark_js_vm run spreads the same array to 1,2,3 while for-of honors the
 * custom iterator) — a pre-existing engine-wide deviation from the spec's
 * iterator protocol, kept here as a consistency regression guard so ArkSteed
 * does not drift away from the interpreter's behavior. Fixing the deviation
 * belongs to the shared admission invariants (kind ladder / prototype bit
 * maintenance), not to the ArkSteed spread path alone.
 */

//! METHOD SC
//! HAS         BranchIfFastSpreadArray
//! HAS         CallBaseConstructorSpread
//! COUNT       CallRuntime SuperCallSpreadWithNewTarget 1

class Wide {
    constructor(a, b, c) {
        this.a = a;
        this.b = b;
        this.c = c;
    }
}

class SC extends Wide {
    constructor(spreadable) {
        super(...spreadable);
    }
}

ArkTools.arkSteedCompileSync(SC);

class MyArr extends Array {}

// custom @@iterator on the subclass prototype: the fast admission still takes
// the slot-copy path (interpreter-consistent; see the note above)
MyArr.prototype[Symbol.iterator] = function* () {
    yield "sub";
    yield "iter";
};

const withIter = MyArr.of(1, 2, 3);
const p = new SC(withIter);
print("subclass-iter: " + p.a + " " + p.b + " " + p.c);

// same subclass without the custom @@iterator: elements arrive identically
const Sub2 = class extends Array {};
const clean = Sub2.of(4, 5, 6);
const q = new SC(clean);
print("subclass-clean: " + q.a + " " + q.b + " " + q.c);

// a plain array stays on the fast path
const r = new SC([7, 8, 9]);
print("plain: " + r.a + " " + r.b + " " + r.c);
