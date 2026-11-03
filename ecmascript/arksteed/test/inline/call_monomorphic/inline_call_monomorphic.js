/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//! PARAMS --enable-force-gc=true --compiler-jit-hotness-threshold=150 --compiler-arksteed-enable-inline=true --compiler-min-inline-call-frequency=0.5 --compiler-max-inline-bytecodes=128 --compiler-arksteed-max-cumulative-inline-bytecode-size=256 --compiler-arksteed-max-graph-nodes-before-inline=1000

//! METHOD callNoArgs
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callReturnUndefined
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callIdentity
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callIdentityMissing
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callArrowIdentity
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callIdentityExtra
//! COUNT DeoptIfTaggedCondition 2
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callThisIdentity
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callReducedMutableTarget
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callOneArg
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callTwoArgs
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callThreeArgs
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callRange
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callWideInlineCandidate
//! COUNT DeoptIfTaggedCondition 0
//! COUNT CallKnownJSFunction 1
//! HAS_NOT Call

//! METHOD callThis0
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callThis1
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callThis2
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callThis3
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callThisRange
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callWithMissingArg
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callWithExtraArgs
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callDefaultArg
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callRestArgs
//! COUNT DeoptIfTaggedCondition 2
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callZeroRestArgs
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callArgumentsAfterEagerDeopt
//! COUNT DeoptIfTaggedCondition 2
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callExactArgumentsAfterEagerDeopt
//! COUNT DeoptIfTaggedCondition 2
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callRestAfterEagerDeopt
//! COUNT DeoptIfTaggedCondition 6
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callArgumentsAfterForcedGC
//! COUNT DeoptIfTaggedCondition 2
//! HAS_NOT CallKnownJSFunction
//! COUNT Call 1

//! METHOD callRestAfterForcedGC
//! COUNT DeoptIfTaggedCondition 5
//! HAS_NOT CallKnownJSFunction
//! COUNT Call 1

//! METHOD callArgumentsAfterLazyDeopt
//! COUNT DeoptIfTaggedCondition 2
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 3

//! METHOD callExactArgumentsAfterLazyDeopt
//! COUNT DeoptIfTaggedCondition 2
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 3

//! METHOD callRestAfterLazyDeopt
//! COUNT DeoptIfTaggedCondition 3
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call
//! COUNT CallKnownBuiltin 1
//! COUNT DeoptIfReferenceNotEqual 3

//! METHOD callStrictUndefinedThis
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callPrimitiveThis
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callAndCatch
//! COUNT DeoptIfTaggedCondition 2
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callNestedAndCatch
//! COUNT DeoptIfTaggedCondition 3
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callColdCatch
//! COUNT DeoptIfTaggedCondition 2
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callNestedColdCatch
//! COUNT_GE DeoptIfTaggedCondition 2
//! COUNT_LE DeoptIfTaggedCondition 5
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callInnerHotCatch
//! COUNT DeoptIfTaggedCondition 1
//! COUNT LoadException 1
//! HAS_NOT Deopt
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callInnerColdCatch
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT LoadException
//! COUNT Deopt 2
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callUncaughtInline
//! COUNT DeoptIfTaggedCondition 2
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callNoReturnWithCatch
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callNoReturnUncaught
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callNoReturnMutable
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callNoReturnLoopBackedge
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callLocalClosure
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callEscapingLocalClosure
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callLocalClosureAfterGC
//! HAS_NOT CallKnownJSFunction
//! COUNT Call 1

//! METHOD callNestedLocalClosure
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callCompatibleClosure
//! COUNT DeoptIfNotHeapObject 1
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callDepthBoundary
//! COUNT DeoptIfTaggedCondition 6
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callCumulativeBudget
//! COUNT DeoptIfTaggedCondition 2
//! COUNT CallKnownJSFunction 2
//! HAS_NOT Call

//! METHOD callAndAllocate
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callMultipleReturns
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callInlineForcedGC
//! COUNT DeoptIfTaggedCondition 2
//! HAS_NOT CallKnownJSFunction
//! COUNT Call 1

//! METHOD callNestedInline
//! COUNT DeoptIfTaggedCondition 2
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callNestedForcedGC
//! COUNT DeoptIfTaggedCondition 3
//! HAS_NOT CallKnownJSFunction
//! COUNT Call 1

//! METHOD callDeepMutableInline
//! COUNT DeoptIfTaggedCondition 3
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callMutableTarget
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD callLateCompiledTarget
//! COUNT DeoptIfTaggedCondition 0
//! COUNT CallKnownJSFunction 1
//! HAS_NOT Call

//! METHOD callPolymorphic
//! HAS_NOT DeoptIfTaggedCondition
//! HAS_NOT CallKnownJSFunction
//! COUNT Call 1

//! METHOD callClassAsFunction
//! COUNT DeoptIfTaggedCondition 1
//! HAS_NOT CallKnownJSFunction
//! COUNT Call 1

//! METHOD callLoopTarget
//! COUNT DeoptIfTaggedCondition 0
//! COUNT CallKnownJSFunction 1
//! HAS_NOT Call

//! METHOD recursiveFib
//! HAS_NOT DeoptIfTaggedCondition
//! COUNT DeoptIfReferenceNotEqual 2
//! COUNT CallSelf 2
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD recursiveMissing
//! HAS_NOT DeoptIfTaggedCondition
//! COUNT DeoptIfReferenceNotEqual 1
//! COUNT CallSelf 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD recursiveExtra
//! HAS_NOT DeoptIfTaggedCondition
//! COUNT DeoptIfReferenceNotEqual 1
//! COUNT CallSelf 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD recursiveThrow
//! COUNT DeoptIfTaggedCondition 1
// Two identity guards: the self-call guard plus the newobjrange ctor-identity
// speculation on `new Error(...)`.
//! COUNT DeoptIfReferenceNotEqual 2
//! COUNT CallSelf 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD recursiveAllocate
//! HAS_NOT DeoptIfTaggedCondition
//! COUNT DeoptIfReferenceNotEqual 1
//! COUNT CallSelf 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD recursiveMutableTargetA
//! HAS_NOT DeoptIfTaggedCondition
//! COUNT DeoptIfReferenceNotEqual 1
//! COUNT CallSelf 1
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

"use strict";

function noArgsTarget() {
    return 7;
}

function callNoArgs() {
    return noArgsTarget();
}

function returnUndefinedTarget() {
}

function callReturnUndefined() {
    return returnUndefinedTarget();
}

function identityTarget(value) {
    return value;
}

function callIdentity(value) {
    return identityTarget(value);
}

function callIdentityMissing() {
    return identityTarget();
}

const arrowIdentityTarget = value => value;
function callArrowIdentity(value) {
    return arrowIdentityTarget(value);
}

let extraEvaluationCount = 0;
function evaluateExtraArgument() {
    extraEvaluationCount++;
    return 99;
}

function callIdentityExtra(value) {
    return identityTarget(value, evaluateExtraArgument());
}

const identityReceiver = { call: identityTarget };
function callThisIdentity(value) {
    return identityReceiver.call(value);
}

function reducedTargetA() {
    return 11;
}

function reducedTargetB() {
    return 29;
}

let reducedMutableTarget = reducedTargetA;
function callReducedMutableTarget() {
    return reducedMutableTarget();
}

function oneArgTarget(value) {
    return value + 1;
}

function callOneArg(value) {
    return oneArgTarget(value);
}

function twoArgsTarget(left, right) {
    return left * 10 + right;
}

function callTwoArgs(left, right) {
    return twoArgsTarget(left, right);
}

function threeArgsTarget(a, b, c) {
    return a * 100 + b * 10 + c;
}

function callThreeArgs(a, b, c) {
    return threeArgsTarget(a, b, c);
}

function rangeTarget(a, b, c, d, e) {
    return a + b + c + d + e;
}

function callRange(a, b, c, d, e) {
    return rangeTarget(a, b, c, d, e);
}

function wideInlineCandidate(a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q) {
    return a + q;
}

function callWideInlineCandidate() {
    return wideInlineCandidate(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17);
}

function receiver0() {
    return this.base;
}

function receiver1(a) {
    return this.base + a;
}

function receiver2(a, b) {
    return this.base + a + b;
}

function receiver3(a, b, c) {
    return this.base + a + b + c;
}

function receiverRange(a, b, c, d, e) {
    return this.base + a + b + c + d + e;
}

const receiver = {
    base: 40,
    call0: receiver0,
    call1: receiver1,
    call2: receiver2,
    call3: receiver3,
    callRange: receiverRange
};

function callThis0() {
    return receiver.call0();
}

function callThis1(a) {
    return receiver.call1(a);
}

function callThis2(a, b) {
    return receiver.call2(a, b);
}

function callThis3(a, b, c) {
    return receiver.call3(a, b, c);
}

function callThisRange(a, b, c, d, e) {
    return receiver.callRange(a, b, c, d, e);
}

function needsTwoArgs(left, right) {
    return left + (right === undefined ? 100 : right);
}

function callWithMissingArg(value) {
    return needsTwoArgs(value);
}

function observesExtraArgs(first) {
    return arguments.length * 100 + first + arguments[2];
}

function callWithExtraArgs(a, b, c) {
    return observesExtraArgs(a, b, c);
}

function defaultArgTarget(left, right = 9) {
    return left + right;
}

function callDefaultArg(value) {
    return defaultArgTarget(value);
}

function restArgsTarget(...values) {
    return values.length * 10 + values[1];
}

function callRestArgs(a, b, c) {
    return restArgsTarget(a, b, c);
}

function zeroRestArgsTarget(...values) {
    return values.length;
}

function callZeroRestArgs() {
    return zeroRestArgsTarget();
}

function argumentsResumeTargetA(value) {
    return value + 1;
}

function argumentsResumeTargetB(value) {
    return value + 10;
}

let argumentsResumeTarget = argumentsResumeTargetA;
function argumentsAfterEagerDeoptTarget(first) {
    const values = arguments;
    const resumed = argumentsResumeTarget(first);
    return values.length * 1000 + values[0] * 100 + values[1] * 10 + values[2] + resumed;
}

function callArgumentsAfterEagerDeopt(a, b, c) {
    return argumentsAfterEagerDeoptTarget(a, b, c);
}

let exactArgumentsResumeTarget = argumentsResumeTargetA;
function exactArgumentsAfterEagerDeoptTarget(first, second, third) {
    const resumed = exactArgumentsResumeTarget(first);
    const values = arguments;
    return values.length * 1000 + values[0] * 100 + values[1] * 10 + values[2] + resumed;
}

function callExactArgumentsAfterEagerDeopt(a, b, c) {
    return exactArgumentsAfterEagerDeoptTarget(a, b, c);
}

function restResumeTargetA(value) {
    return value + 1;
}

function restResumeTargetB(value) {
    return value + 10;
}

let restResumeTarget = restResumeTargetA;
function restAfterEagerDeoptTarget(...values) {
    const resumed = restResumeTarget(values[0]);
    return values.length * 1000 + values[0] * 100 + values[1] * 10 + values[2] + resumed;
}

function callRestAfterEagerDeopt(a, b, c) {
    return restAfterEagerDeoptTarget(a, b, c);
}

function argumentsAfterForcedGCTarget(first) {
    const values = arguments;
    ArkTools.forceFullGC();
    return values.length * 1000 + values[0] * 100 + values[1] * 10 + values[2] + first;
}

function callArgumentsAfterForcedGC(a, b, c) {
    return argumentsAfterForcedGCTarget(a, b, c);
}

function restAfterForcedGCTarget(...values) {
    ArkTools.forceFullGC();
    return values.length * 1000 + values[0] * 100 + values[1] * 10 + values[2];
}

function callRestAfterForcedGC(a, b, c) {
    return restAfterForcedGCTarget(a, b, c);
}

class ArgumentsLazyRoot {}
class ArgumentsLazyBase extends ArgumentsLazyRoot {}
class ArgumentsLazyDerived extends ArgumentsLazyBase {}
ArgumentsLazyRoot.prototype.value = 1;
const argumentsLazyObject = new ArgumentsLazyDerived();
let mutateArgumentsLazyDependency = false;
class ArgumentsLazyTrigger {
    constructor() {
        if (mutateArgumentsLazyDependency) {
            ArgumentsLazyBase.prototype.value = 2;
        }
    }
}

class ExactArgumentsLazyRoot {}
class ExactArgumentsLazyBase extends ExactArgumentsLazyRoot {}
class ExactArgumentsLazyDerived extends ExactArgumentsLazyBase {}
ExactArgumentsLazyRoot.prototype.value = 1;
const exactArgumentsLazyObject = new ExactArgumentsLazyDerived();
let mutateExactArgumentsLazyDependency = false;
class ExactArgumentsLazyTrigger {
    constructor() {
        if (mutateExactArgumentsLazyDependency) {
            ExactArgumentsLazyBase.prototype.value = 2;
        }
    }
}

function argumentsAfterLazyDeoptTarget(first) {
    const values = arguments;
    Reflect.construct(ArgumentsLazyTrigger, []);
    return values.length * 100 + values[2] * 10 + argumentsLazyObject.value;
}

function callArgumentsAfterLazyDeopt(a, b, c) {
    return argumentsAfterLazyDeoptTarget(a, b, c);
}

function exactArgumentsAfterLazyDeoptTarget(first, second, third) {
    Reflect.construct(ExactArgumentsLazyTrigger, []);
    const values = arguments;
    return values.length * 100 + values[2] * 10 + exactArgumentsLazyObject.value;
}

function callExactArgumentsAfterLazyDeopt(a, b, c) {
    return exactArgumentsAfterLazyDeoptTarget(a, b, c);
}

class RestLazyRoot {}
class RestLazyBase extends RestLazyRoot {}
class RestLazyDerived extends RestLazyBase {}
RestLazyRoot.prototype.value = 1;
const restLazyObject = new RestLazyDerived();
let mutateRestLazyDependency = false;
class RestLazyTrigger {
    constructor() {
        if (mutateRestLazyDependency) {
            RestLazyBase.prototype.value = 2;
        }
    }
}

function restAfterLazyDeoptTarget(...values) {
    Reflect.construct(RestLazyTrigger, []);
    return values.length * 100 + values[2] * 10 + restLazyObject.value;
}

function callRestAfterLazyDeopt(a, b, c) {
    return restAfterLazyDeoptTarget(a, b, c);
}

function checkResult(name, actual, expected) {
    if (actual === expected) {
        return true;
    }
    print(name + " expected " + expected + ", got " + actual);
    return false;
}

function strictUndefinedThisTarget() {
    return this === undefined;
}

function callStrictUndefinedThis() {
    return strictUndefinedThisTarget();
}

function strictPrimitiveThisTarget() {
    return this === 17;
}

Number.prototype.arkSteedStrictPrimitiveThis = strictPrimitiveThisTarget;

function callPrimitiveThis() {
    return (17).arkSteedStrictPrimitiveThis();
}

let shouldThrow = false;
function throwingTarget(value) {
    if (shouldThrow) {
        throw new Error("known-call");
    }
    return value + 1;
}

function callAndCatch(value) {
    const fallback = value + 89;
    try {
        return throwingTarget(value);
    } catch (error) {
        return fallback;
    }
}

function throwingMiddle(value) {
    return throwingTarget(value) + 1;
}

function callNestedAndCatch(value) {
    const fallback = value + 190;
    try {
        return throwingMiddle(value) + 1;
    } catch (error) {
        return fallback;
    }
}

function callColdCatch(value) {
    const fallback = { value: value + 289 };
    try {
        return throwingTarget(value);
    } catch (error) {
        return fallback.value;
    }
}

function callNestedColdCatch(value) {
    const fallback = { value: value + 390 };
    try {
        return throwingMiddle(value) + 1;
    } catch (error) {
        return fallback.value;
    }
}

let innerShouldThrow = false;
function innerHotCatchTarget(value) {
    const fallback = { value: value + 490 };
    try {
        if (innerShouldThrow) {
            throw value;
        }
        return value + 1;
    } catch (error) {
        return fallback.value;
    }
}

function callInnerHotCatch(value) {
    return innerHotCatchTarget(value);
}

function innerColdCatchTarget(value) {
    const fallback = { value: value + 590 };
    try {
        if (innerShouldThrow) {
            throw value;
        }
        return value + 1;
    } catch (error) {
        return fallback.value;
    }
}

function callInnerColdCatch(value) {
    return innerColdCatchTarget(value);
}

function callUncaughtInline(value) {
    return throwingTarget(value);
}

function alwaysThrowTarget(value) {
    throw value + 700;
}

function noReturnReplacement(value) {
    return value + 900;
}

function callNoReturnWithCatch(value) {
    try {
        alwaysThrowTarget(value);
        return -1;
    } catch (error) {
        return error;
    }
}

function callNoReturnUncaught(value) {
    alwaysThrowTarget(value);
    return -1;
}

let noReturnMutableTarget = alwaysThrowTarget;
function callNoReturnMutable(value) {
    try {
        noReturnMutableTarget(value);
        return 1;
    } catch (error) {
        return error;
    }
}

function callNoReturnLoopBackedge(value) {
    let iteration = 0;
    try {
        while (iteration < 1) {
            iteration++;
            alwaysThrowTarget(value);
        }
    } catch (error) {
        return error + iteration;
    }
    return -1;
}

function callLocalClosure(value) {
    const bias = value + 17;
    const closure = (argument) => argument + bias;
    return closure(2);
}

let escapedLocalClosure;
function callEscapingLocalClosure(value) {
    const closure = (argument) => argument + value;
    escapedLocalClosure = closure;
    return closure(3);
}

function callLocalClosureAfterGC(value) {
    const closure = (argument) => argument + value;
    ArkTools.forceFullGC();
    return closure(4);
}

function callNestedLocalClosure(value) {
    const outer = (argument) => {
        const inner = (delta) => argument + delta + value;
        return inner(2);
    };
    return outer(3);
}

function makeCompatibleClosure(bias) {
    return (value) => value + bias;
}

function incompatibleCompatibleTarget(value) {
    return value + 1000;
}

function callCompatibleClosure(target, value) {
    return target(value);
}

function depthLeaf(value) {
    return value + 1;
}

function depthLevel5(value) {
    return depthLeaf(value) + 1;
}

function depthLevel4(value) {
    return depthLevel5(value) + 1;
}

function depthLevel3(value) {
    return depthLevel4(value) + 1;
}

function depthLevel2(value) {
    return depthLevel3(value) + 1;
}

function depthLevel1(value) {
    return depthLevel2(value) + 1;
}

function callDepthBoundary(value) {
    return depthLevel1(value) + 1;
}

function cumulativeBudgetTarget(value) {
    value += 1;
    value += 2;
    value += 3;
    value += 4;
    value += 5;
    value += 6;
    value += 7;
    value += 8;
    value += 9;
    value += 10;
    value += 11;
    value += 12;
    return value;
}

function callCumulativeBudget(value) {
    return cumulativeBudgetTarget(value) + cumulativeBudgetTarget(value) +
        cumulativeBudgetTarget(value) + cumulativeBudgetTarget(value);
}

function allocatingTarget(value) {
    const result = { value: value + 1 };
    return result.value;
}

function callAndAllocate(value) {
    return allocatingTarget(value);
}

function multipleReturnTarget(value) {
    if (value < 0) {
        return -1;
    }
    if (value === 0) {
        return 0;
    }
    return value + 1;
}

function callMultipleReturns(value) {
    return multipleReturnTarget(value);
}

function inlineForcedGCTarget(value) {
    ArkTools.forceFullGC();
    return value + 1;
}

function callInlineForcedGC(value) {
    const parentOnly = { value: value + 10 };
    return inlineForcedGCTarget(value) + parentOnly.value;
}

function nestedInlineLeaf(value) {
    return value + 1;
}

function nestedInlineMiddle(value) {
    return nestedInlineLeaf(value) + 2;
}

function callNestedInline(value) {
    return nestedInlineMiddle(value) + 3;
}

function nestedGCMiddle(value) {
    const middleOnly = { value: value + 2 };
    return inlineForcedGCTarget(value) + middleOnly.value;
}

function callNestedForcedGC(value) {
    const outerOnly = { value: value + 3 };
    return nestedGCMiddle(value) + outerOnly.value;
}

// Keep the mutable call target at the deepest inline level. Changing it after
// compilation exercises eager deopt reconstruction for the entire inline chain.
function deepMutableLeafA(value) {
    return value + 1;
}

function deepMutableLeafB(value) {
    return value + 10;
}

let deepMutableLeaf = deepMutableLeafA;

function deepMutableInner(value) {
    return deepMutableLeaf(value) + 2;
}

function deepMutableMiddle(value) {
    return deepMutableInner(value) + 3;
}

function callDeepMutableInline(value) {
    return deepMutableMiddle(value) + 4;
}

function mutableTargetA(value) {
    return value + 1;
}

function mutableTargetB(value) {
    return value + 10;
}

let mutableTarget = mutableTargetA;
function callMutableTarget(value) {
    return mutableTarget(value);
}

// Keep this target above the per-callee inline budget so this fixture continues
// to exercise a known call whose compiled entry appears after its caller.
function lateCompiledTarget(value) {
    value += 1;
    value += 2;
    value += 3;
    value += 4;
    value += 5;
    value += 6;
    value += 7;
    value += 8;
    value += 9;
    value += 10;
    value += 11;
    value += 12;
    value += 13;
    value += 14;
    return value - 85;
}

function callLateCompiledTarget(value) {
    return lateCompiledTarget(value);
}

function increment(value) {
    return value + 1;
}

function double(value) {
    return value * 2;
}

function callPolymorphic(target, value) {
    return target(value);
}

class ClassTarget {
}

function callClassAsFunction() {
    try {
        ClassTarget();
    } catch (error) {
        return error instanceof TypeError;
    }
    return false;
}

function loopTarget(limit) {
    let sum = 0;
    for (let i = 0; i < limit; i++) {
        sum += i;
    }
    return sum;
}

function callLoopTarget(limit) {
    return loopTarget(limit);
}

function recursiveFib(value) {
    if (value < 2) {
        return value;
    }
    return recursiveFib(value - 1) + recursiveFib(value - 2);
}

function recursiveMissing(value, marker) {
    if (value === 0) {
        return marker === undefined;
    }
    return recursiveMissing(value - 1);
}

function recursiveExtra(value) {
    if (value === 0) {
        return arguments.length;
    }
    return recursiveExtra(value - 1, 7, 8);
}

function recursiveThrow(value) {
    if (value === 0) {
        throw new Error("self-call");
    }
    return recursiveThrow(value - 1);
}

function callRecursiveAndCatch(value) {
    try {
        return recursiveThrow(value);
    } catch (error) {
        return error.message === "self-call";
    }
}

function recursiveAllocate(value) {
    const box = { value };
    if (value === 0) {
        return box.value;
    }
    return box.value + recursiveAllocate(value - 1);
}

let recursiveMutableTarget;
function recursiveMutableTargetA(value) {
    if (value === 0) {
        return 1;
    }
    return recursiveMutableTarget(value - 1);
}

function recursiveMutableTargetB(value) {
    return value + 40;
}

recursiveMutableTarget = recursiveMutableTargetA;

let compatibleWarmupClosure;
for (let i = 0; i < 64; i++) {
    callNoArgs();
    callReturnUndefined();
    callIdentity(i);
    callIdentityMissing();
    callArrowIdentity(i);
    callIdentityExtra(i);
    callThisIdentity(i);
    callReducedMutableTarget();
    callOneArg(i);
    callTwoArgs(i, 2);
    callThreeArgs(1, 2, 3);
    callRange(1, 2, 3, 4, 5);
    callWideInlineCandidate();
    callThis0();
    callThis1(1);
    callThis2(1, 2);
    callThis3(1, 2, 3);
    callThisRange(1, 2, 3, 4, 5);
    callWithMissingArg(i);
    callWithExtraArgs(1, 2, 3);
    callDefaultArg(i);
    callRestArgs(1, 2, 3);
    callZeroRestArgs();
    callArgumentsAfterEagerDeopt(1, 2, 3);
    callExactArgumentsAfterEagerDeopt(1, 2, 3);
    callRestAfterEagerDeopt(1, 2, 3);
    callArgumentsAfterLazyDeopt(1, 2, 3);
    callExactArgumentsAfterLazyDeopt(1, 2, 3);
    callRestAfterLazyDeopt(1, 2, 3);
    callStrictUndefinedThis();
    callPrimitiveThis();
    callAndCatch(i);
    callNestedAndCatch(i);
    callColdCatch(i);
    callNestedColdCatch(i);
    callInnerHotCatch(i);
    callInnerColdCatch(i);
    callUncaughtInline(i);
    callNoReturnWithCatch(i);
    try {
        callNoReturnUncaught(i);
    } catch (error) {
    }
    callNoReturnMutable(i);
    callNoReturnLoopBackedge(i);
    callLocalClosure(i);
    callEscapingLocalClosure(i);
    callNestedLocalClosure(i);
    compatibleWarmupClosure = makeCompatibleClosure(i);
    callCompatibleClosure(compatibleWarmupClosure, 1);
    callDepthBoundary(i);
    callCumulativeBudget(i);
    callAndAllocate(i);
    callMultipleReturns(i - 1);
    callNestedInline(i);
    callDeepMutableInline(i);
    callMutableTarget(i);
    // One call is enough to record the exact target. Repeating this large
    // callee would cross the automatic JIT hotness threshold before the test
    // can exercise a known call whose target is not compiled yet.
    if (i === 0) {
        callLateCompiledTarget(i);
    }
    callPolymorphic((i & 1) === 0 ? increment : double, i);
    callClassAsFunction();
    callLoopTarget(8);
    recursiveFib(8);
    recursiveMissing(3, 1);
    recursiveExtra(3);
    try {
        recursiveThrow(2);
    } catch (error) {
    }
    recursiveAllocate(8);
    recursiveMutableTargetA(3);
}

// A single profiling execution is sufficient for the forced-GC call sites.
// Keep them out of the shared 64-iteration warmup so this correctness test does
// not perform hundreds of full collections before compilation.
callArgumentsAfterForcedGC(1, 2, 3);
callRestAfterForcedGC(1, 2, 3);
callLocalClosureAfterGC(1);
callInlineForcedGC(1);
callNestedForcedGC(1);

// Profile only the hot catch handlers so graph inlining exercises compiled
// exception edges. The cold handlers contain unprofiled property loads and
// exercise cross-inline exception lazy deoptimization.
shouldThrow = true;
for (let i = 0; i < 64; i++) {
    callAndCatch(i);
    callNestedAndCatch(i);
}
shouldThrow = false;

// Keep one child-local catch hot while leaving the other one unexecuted. The
// inlined compilation unit consumes the callee's own insufficient-profile flags:
// the hot handler is compiled, while the cold handler uses deoptimization.
innerShouldThrow = true;
for (let i = 0; i < 64; i++) {
    callInnerHotCatch(i);
}
innerShouldThrow = false;

const initiallyCompiled = [
    noArgsTarget, returnUndefinedTarget, identityTarget, arrowIdentityTarget, evaluateExtraArgument,
    reducedTargetA, reducedTargetB,
    oneArgTarget, twoArgsTarget, threeArgsTarget, rangeTarget, wideInlineCandidate,
    receiver0, receiver1, receiver2, receiver3, receiverRange,
    needsTwoArgs, observesExtraArgs, defaultArgTarget, restArgsTarget, zeroRestArgsTarget,
    argumentsResumeTargetA, argumentsResumeTargetB, argumentsAfterEagerDeoptTarget,
    exactArgumentsAfterEagerDeoptTarget,
    restResumeTargetA, restResumeTargetB, restAfterEagerDeoptTarget,
    argumentsAfterForcedGCTarget, restAfterForcedGCTarget,
    argumentsAfterLazyDeoptTarget, restAfterLazyDeoptTarget,
    exactArgumentsAfterLazyDeoptTarget,
    strictUndefinedThisTarget, strictPrimitiveThisTarget,
    throwingTarget, throwingMiddle, innerHotCatchTarget, innerColdCatchTarget,
    alwaysThrowTarget, noReturnReplacement,
    allocatingTarget, multipleReturnTarget, inlineForcedGCTarget,
    nestedInlineLeaf, nestedInlineMiddle, nestedGCMiddle,
    depthLeaf, depthLevel5, depthLevel4, depthLevel3, depthLevel2, depthLevel1,
    cumulativeBudgetTarget,
    deepMutableLeafA, deepMutableLeafB, deepMutableInner, deepMutableMiddle,
    mutableTargetA, mutableTargetB,
    increment, double,
    loopTarget,
    recursiveFib, recursiveMissing, recursiveExtra, recursiveThrow, recursiveAllocate,
    recursiveMutableTargetA, recursiveMutableTargetB,
    callNoArgs, callReturnUndefined, callIdentity, callIdentityMissing, callArrowIdentity,
    callIdentityExtra, callThisIdentity, callReducedMutableTarget,
    callOneArg, callTwoArgs, callThreeArgs, callRange, callWideInlineCandidate,
    callThis0, callThis1, callThis2, callThis3, callThisRange,
    callWithMissingArg, callWithExtraArgs, callDefaultArg, callRestArgs, callZeroRestArgs,
    callArgumentsAfterEagerDeopt, callRestAfterEagerDeopt,
    callExactArgumentsAfterEagerDeopt,
    callArgumentsAfterForcedGC, callRestAfterForcedGC,
    callArgumentsAfterLazyDeopt, callRestAfterLazyDeopt,
    callExactArgumentsAfterLazyDeopt,
    callStrictUndefinedThis, callPrimitiveThis,
    callAndCatch, callNestedAndCatch, callColdCatch, callNestedColdCatch,
    callInnerHotCatch, callInnerColdCatch, callUncaughtInline,
    callNoReturnWithCatch, callNoReturnUncaught, callNoReturnMutable, callNoReturnLoopBackedge,
    callLocalClosure, callEscapingLocalClosure,
    callLocalClosureAfterGC, callNestedLocalClosure, callCompatibleClosure,
    callDepthBoundary,
    callCumulativeBudget,
    callAndAllocate,
    callMultipleReturns, callInlineForcedGC, callNestedInline,
    callNestedForcedGC, callDeepMutableInline,
    callMutableTarget, callLateCompiledTarget, callPolymorphic, callClassAsFunction, callLoopTarget,
    callRecursiveAndCatch
];

let ok = true;
let firstFailureReported = false;
function reportCheckpoint(name) {
    if (!firstFailureReported && !ok) {
        print("first failure after " + name);
        firstFailureReported = true;
    }
}

for (const target of initiallyCompiled) {
    ok = ArkTools.arkSteedCompileSync(target) && ok;
}
for (const target of initiallyCompiled) {
    ArkTools.waitJitCompileFinish(target);
    ok = ArkTools.isAOTCompiled(target) && ok;
}
reportCheckpoint("initial compilation");

ok = callNoArgs() === 7 && ok;
ok = callReturnUndefined() === undefined && ok;
ok = callIdentity(37) === 37 && ok;
ok = callIdentityMissing() === undefined && ok;
ok = callArrowIdentity(39) === 39 && ok;
const extraEvaluationBefore = extraEvaluationCount;
ok = callIdentityExtra(41) === 41 && ok;
ok = extraEvaluationCount === extraEvaluationBefore + 1 && ok;
ok = callThisIdentity(43) === 43 && ok;
ok = callReducedMutableTarget() === 11 && ok;
reducedMutableTarget = reducedTargetB;
ok = callReducedMutableTarget() === 29 && ok;
ok = callOneArg(6) === 7 && ok;
ok = callTwoArgs(3, 4) === 34 && ok;
ok = callThreeArgs(1, 2, 3) === 123 && ok;
ok = callRange(1, 2, 3, 4, 5) === 15 && ok;
ok = callWideInlineCandidate() === 18 && ok;
ok = callThis0() === 40 && ok;
ok = callThis1(1) === 41 && ok;
ok = callThis2(1, 2) === 43 && ok;
ok = callThis3(1, 2, 3) === 46 && ok;
ok = callThisRange(1, 2, 3, 4, 5) === 55 && ok;
reportCheckpoint("basic calls");

// Inlined calls fill missing formals with undefined and preserve all actual
// arguments for arguments/rest materialization.
ok = checkResult("callWithMissingArg", callWithMissingArg(1), 101) && ok;
ok = checkResult("callWithExtraArgs", callWithExtraArgs(1, 2, 3), 304) && ok;
ok = checkResult("callDefaultArg", callDefaultArg(1), 10) && ok;
ok = checkResult("callRestArgs", callRestArgs(1, 2, 3), 32) && ok;
ok = checkResult("callZeroRestArgs", callZeroRestArgs(), 0) && ok;
ok = checkResult(
    "callArgumentsAfterEagerDeopt before mutation", callArgumentsAfterEagerDeopt(1, 2, 3), 3125) && ok;
argumentsResumeTarget = argumentsResumeTargetB;
ok = checkResult(
    "callArgumentsAfterEagerDeopt after mutation", callArgumentsAfterEagerDeopt(1, 2, 3), 3134) && ok;
ok = checkResult(
    "callExactArgumentsAfterEagerDeopt before mutation", callExactArgumentsAfterEagerDeopt(1, 2, 3), 3125) && ok;
exactArgumentsResumeTarget = argumentsResumeTargetB;
ok = checkResult(
    "callExactArgumentsAfterEagerDeopt after mutation", callExactArgumentsAfterEagerDeopt(1, 2, 3), 3134) && ok;
ok = checkResult("callRestAfterEagerDeopt before mutation", callRestAfterEagerDeopt(1, 2, 3), 3125) && ok;
restResumeTarget = restResumeTargetB;
ok = checkResult("callRestAfterEagerDeopt after mutation", callRestAfterEagerDeopt(1, 2, 3), 3134) && ok;
ok = checkResult(
    "callArgumentsAfterForcedGC", callArgumentsAfterForcedGC(1, 2, 3), 3124) && ok;
ok = checkResult("callRestAfterForcedGC", callRestAfterForcedGC(1, 2, 3), 3123) && ok;
ok = checkResult(
    "callArgumentsAfterLazyDeopt before mutation", callArgumentsAfterLazyDeopt(1, 2, 3), 331) && ok;
ok = checkResult(
    "callExactArgumentsAfterLazyDeopt before mutation", callExactArgumentsAfterLazyDeopt(1, 2, 3), 331) && ok;
mutateArgumentsLazyDependency = true;
ok = checkResult(
    "callArgumentsAfterLazyDeopt after mutation", callArgumentsAfterLazyDeopt(1, 2, 3), 332) && ok;
mutateExactArgumentsLazyDependency = true;
ok = checkResult(
    "callExactArgumentsAfterLazyDeopt after mutation", callExactArgumentsAfterLazyDeopt(1, 2, 3), 332) && ok;
ok = checkResult("callRestAfterLazyDeopt before mutation", callRestAfterLazyDeopt(1, 2, 3), 331) && ok;
mutateRestLazyDependency = true;
ok = checkResult("callRestAfterLazyDeopt after mutation", callRestAfterLazyDeopt(1, 2, 3), 332) && ok;
reportCheckpoint("inline arguments");

ok = callStrictUndefinedThis() && ok;
ok = callPrimitiveThis() && ok;
reportCheckpoint("this binding");

ok = callAndCatch(10) === 11 && ok;
ok = callNestedAndCatch(10) === 13 && ok;
ok = callColdCatch(10) === 11 && ok;
ok = callNestedColdCatch(10) === 13 && ok;
ok = callInnerHotCatch(10) === 11 && ok;
ok = callInnerColdCatch(10) === 11 && ok;
ok = callUncaughtInline(10) === 11 && ok;
shouldThrow = true;
ok = callAndCatch(10) === 99 && ok;
ok = callNestedAndCatch(10) === 200 && ok;
ok = callColdCatch(10) === 299 && ok;
ok = callNestedColdCatch(10) === 400 && ok;
let escapedError;
try {
    callUncaughtInline(10);
} catch (error) {
    escapedError = error;
}
ok = escapedError !== undefined && escapedError.message === "known-call" && ok;

ok = callNoReturnWithCatch(10) === 710 && ok;
let escapedNoReturnValue;
try {
    callNoReturnUncaught(10);
} catch (error) {
    escapedNoReturnValue = error;
}
ok = escapedNoReturnValue === 710 && ok;
ok = callNoReturnMutable(10) === 710 && ok;
noReturnMutableTarget = noReturnReplacement;
ok = callNoReturnMutable(10) === 1 && ok;
ok = checkResult("callNoReturnLoopBackedge", callNoReturnLoopBackedge(10), 711) && ok;
innerShouldThrow = true;
ok = callInnerHotCatch(10) === 500 && ok;
ok = callInnerColdCatch(10) === 600 && ok;
reportCheckpoint("inline exceptions");

ok = callLocalClosure(10) === 29 && ok;
const previousEscapedLocalClosure = escapedLocalClosure;
ok = callEscapingLocalClosure(10) === 13 && ok;
const capturedLocalClosure = escapedLocalClosure;
ok = previousEscapedLocalClosure !== capturedLocalClosure && ok;
ok = callEscapingLocalClosure(20) === 23 && ok;
ok = capturedLocalClosure(3) === 13 && ok;
ok = callLocalClosureAfterGC(10) === 14 && ok;
ok = callNestedLocalClosure(10) === 15 && ok;
const compatibleClosureA = makeCompatibleClosure(10);
const compatibleClosureB = makeCompatibleClosure(20);
ok = compatibleClosureA !== compatibleClosureB && ok;
ok = callCompatibleClosure(compatibleClosureA, 1) === 11 && ok;
ok = callCompatibleClosure(compatibleClosureB, 1) === 21 && ok;
ok = callCompatibleClosure(incompatibleCompatibleTarget, 1) === 1001 && ok;
ok = callDepthBoundary(1) === 8 && ok;
ok = callCumulativeBudget(1) === 316 && ok;
reportCheckpoint("local closures and budgets");

ArkTools.forceFullGC();
ok = callAndAllocate(41) === 42 && ok;
ok = callMultipleReturns(-1) === -1 && ok;
ok = callMultipleReturns(0) === 0 && ok;
ok = callMultipleReturns(41) === 42 && ok;
ok = callInlineForcedGC(15) === 41 && ok;
ok = callNestedInline(17) === 23 && ok;
ok = callNestedForcedGC(10) === 36 && ok;
ok = callDeepMutableInline(1) === 11 && ok;
deepMutableLeaf = deepMutableLeafB;
ok = callDeepMutableInline(1) === 20 && ok;
reportCheckpoint("allocation and nested inlining");

ok = callMutableTarget(1) === 2 && ok;
mutableTarget = mutableTargetB;
ok = callMutableTarget(1) === 11 && ok;
reportCheckpoint("mutable known calls");

// The caller was compiled with exact-target feedback while the callee had no
// compiled entry. The first call must take the generic fallback. The same
// caller must use the compiled entry after the callee is compiled later.
ok = checkResult(
    "late target initially uncompiled", ArkTools.isAOTCompiled(lateCompiledTarget), false) && ok;
ok = checkResult("late target generic call", callLateCompiledTarget(1), 21) && ok;
ok = checkResult(
    "late target compile request", ArkTools.arkSteedCompileSync(lateCompiledTarget), true) && ok;
ArkTools.waitJitCompileFinish(lateCompiledTarget);
ok = checkResult(
    "late target compiled state", ArkTools.isAOTCompiled(lateCompiledTarget), true) && ok;
ok = checkResult("late target compiled call", callLateCompiledTarget(2), 22) && ok;
reportCheckpoint("late compiled target");

ok = callPolymorphic(increment, 20) === 21 && ok;
ok = callPolymorphic(double, 20) === 40 && ok;
ok = callClassAsFunction() && ok;
ok = callLoopTarget(10) === 45 && ok;
reportCheckpoint("polymorphic and loop calls");

ok = recursiveFib(10) === 55 && ok;
ok = recursiveMissing(3, 1) && ok;
ok = recursiveExtra(3) === 3 && ok;
ok = callRecursiveAndCatch(4) && ok;
ArkTools.forceFullGC();
ok = recursiveAllocate(20) === 210 && ok;
ok = recursiveMutableTargetA(3) === 1 && ok;
recursiveMutableTarget = recursiveMutableTargetB;
ok = recursiveMutableTargetA(3) === 42 && ok;
reportCheckpoint("recursive calls");

delete Number.prototype.arkSteedStrictPrimitiveThis;
print(ok ? "PASS" : "FAIL");
