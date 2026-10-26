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
 *
 * The warm-up at the bottom is the source of the profile of the fixture
 * methods: the interpreter creates the `ProfileTypeInfo` of a method after its
 * own hotness counter crosses zero (about eleven calls of a method this small,
 * `EcmaInterpreter::GetHotnessCounter`), and every later miss fills the IC slot
 * of the executed site. Nothing here is measured by the fixture; the fixture
 * only verifies the state the warm-up left behind.
 *
 * Call counts of the warm-up (each one far below
 * `ArkSteedBytecodeFixture::MAX_WARMUP_CALLS_PER_METHOD`, which is 64):
 *   front-end kinds 12, NAMED_MONO 24, NAMED_POLY_LIMIT 27, NAMED_MEGA 28,
 *   NAMED_POLY_OVERFLOW 24, ELEMENT_MONO 24, ELEMENT_POLY_LIMIT 27,
 *   GLOBAL_ACCESS 24, CALL_JS 24, CALL_BUILTIN 24, OPERATION 24,
 *   EMPTY_SLOT 24.
 * The JIT hotness threshold of the fixture VM is the fixture's
 * `JIT_HOTNESS_THRESHOLD` (30000) and is set on the VM options before the
 * script runs, so no warm-up call count can reach the compilation trigger.
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
 * One arithmetic site per operation-hint category, sampled by the warm-up with
 * the operand types of that category: integer sum, number product, string
 * concatenation, number-or-string concatenation, an operand pair the fast path
 * cannot classify (`ANY`), a big-integer strict comparison, a boolean
 * conversion and an undefined-or-null conversion. The block guarded by
 * `neverFlag` is never sampled by the warm-up: its sites stay uninitialized and
 * are the slots the fixture writes with a raw `pgo::PGOSampleType` when a hint
 * cannot be sampled naturally.
 */
function fixtureOperation(intLeft, intRight, text, number, neverFlag, objectValue, bigValue, nullValue, boolValue) {
    var intSum = intLeft + intRight;
    var numberProduct = intLeft * 1.5;
    var textJoin = text + "!";
    var mixedJoin = number + "x";
    var objectSum = intLeft + objectValue;
    var bigSum = bigValue + 1n;
    var bigSame = bigValue === bigValue;
    var booleanNot = !boolValue;
    var nullNot = !nullValue;
    if (neverFlag) {
        var spareAdd = intLeft + 1;
        var spareMul = intLeft * 1.5;
        var spareJoin = text + "!";
    }
    return intSum + numberProduct + textJoin + mixedJoin + objectSum +
        (booleanNot ? 1 : 0) + (nullNot ? 1 : 0) + (bigSame ? 1 : 0);
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

/*
 * Warm-up. The receivers are kept on the global object for the whole life of
 * the VM, so the weak `JSHClass` references the IC slots hold stay valid while
 * the fixture verifies them.
 */
(function warmUpFixtureMethods() {
    var mono = { value: 1 };
    var polyA = { alpha: 0, value: 1 };
    var polyB = { value: 1, beta: 0 };
    var polyC = { gamma: 0, delta: 0, value: 1 };
    var megaD = { epsilon: 0, zeta: 0, value: 1 };
    var objectValue = { field: 1, other: 2 };
    var elementMono = [1, 2, 3];
    var elementPolyA = [1.5, 2.5];
    var elementPolyB = ["a", "b"];
    var elementPolyC = [1, , 3];
    globalThis.__arksteedFixtureKeepAlive = [
        mono, polyA, polyB, polyC, megaD, objectValue,
        elementMono, elementPolyA, elementPolyB, elementPolyC
    ];

    var call = 0;
    for (call = 0; call < 12; call++) {
        fixtureLinear(call, 3);
        fixtureBranch(call, call % 2 === 0 ? 1 : -1);
        fixtureLoop(4);
        fixtureCriticalEdge(4);
        fixtureTryCatch(call);
    }

    // NAMED_MONO: a single receiver shape.
    for (call = 0; call < 24; call++) {
        fixtureNamedMono(mono);
    }

    // NAMED_POLY_LIMIT: one shape first, then each remaining shape once, so the
    // polymorphic array stops exactly at the IC limit.
    for (call = 0; call < 24; call++) {
        fixtureNamedPolyLimit(mono);
    }
    fixtureNamedPolyLimit(polyA);
    fixtureNamedPolyLimit(polyB);
    fixtureNamedPolyLimit(polyC);

    // NAMED_MEGA: one shape more than the IC limit allows.
    for (call = 0; call < 24; call++) {
        fixtureNamedMega(mono);
    }
    fixtureNamedMega(polyA);
    fixtureNamedMega(polyB);
    fixtureNamedMega(polyC);
    fixtureNamedMega(megaD);

    // NAMED_POLY_OVERFLOW: a single natural shape; the fixture overwrites the
    // slot with the constructed array above the IC limit.
    for (call = 0; call < 24; call++) {
        fixtureNamedPolyOverflow(mono);
    }

    // ELEMENT_MONO and ELEMENT_POLY_LIMIT: the same progression on element
    // access, with four array shapes of distinct element kinds.
    for (call = 0; call < 24; call++) {
        fixtureElementMono(elementMono);
        fixtureElementPolyLimit(elementMono);
    }
    fixtureElementPolyLimit(elementPolyA);
    fixtureElementPolyLimit(elementPolyB);
    fixtureElementPolyLimit(elementPolyC);

    for (call = 0; call < 24; call++) {
        fixtureGlobalAccess();
        fixtureCallJs(call);
        fixtureCallBuiltin("7");
        fixtureOperation(call, 3, "s", 4, false, objectValue, 8n, null, true);
        // The access site of EMPTY_SLOT stays unexecuted: `flag` is false.
        fixtureEmptySlot(mono, false);
    }
})();
