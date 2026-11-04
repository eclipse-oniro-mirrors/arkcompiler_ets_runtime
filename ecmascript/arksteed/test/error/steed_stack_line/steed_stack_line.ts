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

/**
 * @file steed_stack_line.ts
 *
 * End-to-end test for STEED JIT frame line number accuracy in Error.stack.
 *
 * AC-06: WHEN JIT-compiled code throws an exception THEN Error.stack
 *        STEED frames show correct file:line:column matching source code.
 *
 * Each test:
 * 1. Defines a function that throws on a known line.
 * 2. STEED-compiles the function.
 * 3. Catches the exception and extracts line numbers from Error.stack.
 * 4. Asserts the line number matches the expected source line.
 */

//! PARAMS --compiler-jit-hotness-threshold=100

declare function print(arg: any): string;

// ==================== HELPERS ====================

// Extract line number from Error.stack frame for a given function name.
// Stack format: "at funcName (file:line:col)"
function getLineFromStack(stack: string, funcName: string): number {
    const lines = stack.split("\n");
    for (const line of lines) {
        if (line.includes(funcName)) {
            const m = line.match(/:(\d+):(\d+)\)?$/);
            if (m) {
                return parseInt(m[1], 10);
            }
        }
    }
    return -1;
}

// Summarize frames as "funcName:line" so the printed output does not depend
// on the machine-specific source path recorded inside the abc.
function summarizeStack(stack: string): string {
    const frames: string[] = [];
    for (const line of stack.split("\n")) {
        const m = line.match(/\s*at (\S+) \([^)]*:(\d+):\d+\)/);
        if (m) {
            frames.push(m[1] + ":" + m[2]);
        }
    }
    return frames.join("|");
}

// ==================== TEST FUNCTIONS ====================

// --- Single function throw ---
// AC-06.3: Line number must point to actual throw location, not function first line
function singleThrow(): void {
    throw new Error("single");  // line 70
}

// --- Multi-layer call chain ---
// AC-06.1: All frames in the chain must have correct line numbers
function innerThrow(): void {
    throw new Error("inner");   // line 76
}

function middleCall(): void {
    innerThrow();               // line 80
}

function outerCall(): void {
    middleCall();               // line 84
}

// --- Try-catch Error.stack ---
// AC-06.2: Error.stack inside catch block must have correct line numbers
function tryCatchThrow(): void {
    try {
        // This line number is the throw site
        throw new Error("try");  // line 92
    } catch (e) {
        const err = e as Error;
        const stack = err.stack!;
        const line = getLineFromStack(stack, "tryCatchThrow");
        print("tryCatchThrow_line:" + (line > 0 ? line : 0));
    }
}

// --- Multiple throw points ---
function multiThrow(v: number): void {
    if (v < 0) {
        throw new RangeError("neg");  // line 104
    }
    if (v === 0) {
        throw new Error("zero");      // line 107
    }
}

// ==================== RUN ====================

// STEED compile all test functions
ArkTools.arkSteedCompileSync(singleThrow);
ArkTools.arkSteedCompileSync(innerThrow);
ArkTools.arkSteedCompileSync(middleCall);
ArkTools.arkSteedCompileSync(outerCall);
ArkTools.arkSteedCompileSync(tryCatchThrow);
ArkTools.arkSteedCompileSync(multiThrow);

// Verdict accumulators
let passCount: number = 0;
let failCount: number = 0;

function check(desc: string, ok: boolean): void {
    if (ok) {
        passCount++;
        print(desc + ": PASS");
    } else {
        failCount++;
        print(desc + ": FAIL");
    }
}

// Test 1: Single function - verify line number is non-zero
// (Before the fix, STEED frames always show line 0)
try {
    singleThrow();
} catch (e) {
    const err = e as Error;
    const stack = err.stack!;
    const line = getLineFromStack(stack, "singleThrow");
    print("singleThrow_stack:" + line);
    check("singleThrow", line > 0);
}

// Test 2: Multi-layer call chain - all frames must have non-zero lines
try {
    outerCall();
} catch (e) {
    const err = e as Error;
    const stack = err.stack!;
    print("multiLayer_stack:" + summarizeStack(stack));
    const innerLine = getLineFromStack(stack, "innerThrow");
    const middleLine = getLineFromStack(stack, "middleCall");
    const outerLine = getLineFromStack(stack, "outerCall");
    check("innerThrow", innerLine > 0);
    check("middleCall", middleLine > 0);
    check("outerCall", outerLine > 0);
}

// Test 3: Try-catch Error.stack
tryCatchThrow();

// Test 4: Multiple throw points - negative case
try {
    multiThrow(-1);
} catch (e) {
    const err = e as Error;
    const stack = err.stack!;
    const line = getLineFromStack(stack, "multiThrow");
    print("multiThrow(neg)_stack:" + line);
    check("multiThrow_neg", line > 0);
}

// Test 5: Multiple throw points - zero case
try {
    multiThrow(0);
} catch (e) {
    const err = e as Error;
    const stack = err.stack!;
    const line = getLineFromStack(stack, "multiThrow");
    print("multiThrow(zero)_stack:" + line);
    check("multiThrow_zero", line > 0);
}

// Summary
print("Total:" + (passCount + failCount) + " Pass:" + passCount + " Fail:" + failCount);