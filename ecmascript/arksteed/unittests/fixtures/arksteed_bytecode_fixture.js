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
 * Bytecode source of the ArkSteed host unit-test fixture
 * (ecmascript/arksteed/unittests/fixtures/bytecode_fixture.h).
 *
 * This file is a build input of the GN action `gen_arksteed_bytecode_fixture_abc`
 * (template `es2abc_gen_abc`), compiled in script mode. It is not a case of the
 * JS/TS runner: it lives outside `ecmascript/arksteed/test/` and has no
 * `expected_output.txt`.
 *
 * The script declares one function per `FixtureMethodKind` value and publishes
 * them on `globalThis.__arksteedFixtures` under the enumerator names; every kind
 * owns its own function object, so no two kinds share a function.
 *
 * Bytecode forms carried by the front-end kinds:
 *   LINEAR        linear code, no conditional jump;
 *   BRANCH        conditional jump;
 *   LOOP          loop with a back edge;
 *   CRITICAL_EDGE conditional jump inside a loop;
 *   TRY_CATCH     try region with a catch block reachable through the throw.
 * Every other kind carries the access site its profile state is observed on:
 * named access (`NAMED_*`), element access (`ELEMENT_*`), a global variable with
 * its property box (`GLOBAL_ACCESS`), a JS function call (`CALL_JS`), a builtin
 * call (`CALL_BUILTIN`), the arithmetic owners of the operation hints
 * (`OPERATION`) and a site that stays unexecuted (`EMPTY_SLOT`).
 */

var fixtureGlobalSlot = 41;

function fixtureLinear(left, right) {
    var value = left + right;
    value = value * 2;
    return value - 1;
}

function fixtureBranch(left, right) {
    var value = left;
    if (right > 0) {
        value = left + right;
    } else {
        value = left - right;
    }
    return value;
}

function fixtureLoop(count) {
    var sum = 0;
    for (var index = 0; index < count; index++) {
        sum += index;
    }
    return sum;
}

function fixtureCriticalEdge(count) {
    var sum = 0;
    for (var index = 0; index < count; index++) {
        if (index % 2 === 0) {
            sum += index;
        } else {
            sum -= index;
        }
    }
    return sum;
}

function fixtureTryCatch(value) {
    try {
        if (value < 0) {
            throw new Error("negative");
        }
        return value + 1;
    } catch (error) {
        return -1;
    }
}

/*
 * The four named-access kinds share one access shape on purpose: the profile
 * state of each of them (monomorphic, polymorphic at the IC limit, megamorphic,
 * polymorphic above the ArkSteed limit) is produced by the receiver shapes the
 * warm-up feeds into its own method, not by the source text.
 */
function fixtureNamedMono(receiver) {
    return receiver.value;
}

function fixtureNamedPolyLimit(receiver) {
    return receiver.value;
}

function fixtureNamedMega(receiver) {
    return receiver.value;
}

function fixtureNamedPolyOverflow(receiver) {
    return receiver.value;
}

function fixtureElementMono(receiver) {
    return receiver[0];
}

function fixtureElementPolyLimit(receiver) {
    return receiver[0];
}

function fixtureGlobalAccess() {
    return fixtureGlobalSlot;
}

function fixtureJsCallee(value) {
    return value + 1;
}

function fixtureCallJs(value) {
    return fixtureJsCallee(value);
}

function fixtureCallBuiltin(value) {
    return parseInt(value);
}

/*
 * One arithmetic site per operation-hint category: integer, double, string,
 * number-or-string, object-valued, boolean, null/undefined and bigint inputs.
 * Hints that natural sampling cannot produce are written into the profile by
 * the fixture itself.
 */
function fixtureOperation(left, right, text, flag, holder, bigValue) {
    var add = left + right;
    var scaled = left * 1.5;
    var joined = text + "!";
    var mixed = right + "x";
    var field = holder.field + holder.other;
    var guarded = flag ? add : scaled;
    var compared = (holder.missing === null) ? 1 : 0;
    var wide = bigValue + 1n;
    return add + scaled + joined + mixed + field + guarded + compared;
}

/*
 * The named-access site is only reached when `flag` is truthy; a warm-up that
 * never takes that branch leaves the profile slot of the site undefined.
 */
function fixtureEmptySlot(receiver, flag) {
    if (flag) {
        return receiver.value;
    }
    return 0;
}

globalThis.__arksteedFixtures = {
    LINEAR: fixtureLinear,
    BRANCH: fixtureBranch,
    LOOP: fixtureLoop,
    CRITICAL_EDGE: fixtureCriticalEdge,
    TRY_CATCH: fixtureTryCatch,
    NAMED_MONO: fixtureNamedMono,
    NAMED_POLY_LIMIT: fixtureNamedPolyLimit,
    NAMED_MEGA: fixtureNamedMega,
    NAMED_POLY_OVERFLOW: fixtureNamedPolyOverflow,
    ELEMENT_MONO: fixtureElementMono,
    ELEMENT_POLY_LIMIT: fixtureElementPolyLimit,
    GLOBAL_ACCESS: fixtureGlobalAccess,
    CALL_JS: fixtureCallJs,
    CALL_BUILTIN: fixtureCallBuiltin,
    OPERATION: fixtureOperation,
    EMPTY_SLOT: fixtureEmptySlot,
};
