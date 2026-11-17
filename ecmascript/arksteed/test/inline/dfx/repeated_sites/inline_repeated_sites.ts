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

//! PARAMS --compiler-arksteed-enable-inline=true --compiler-min-inline-call-frequency=0.5 --compiler-max-inline-bytecodes=128 --compiler-arksteed-max-cumulative-inline-bytecode-size=256 --compiler-arksteed-max-graph-nodes-before-inline=1000

//! METHOD direct
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD nested
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

//! METHOD captureRoot
//! COUNT CallKnownJSFunction 1

declare function print(arg: any): string;

function helper(value: number): number {
    if (value !== 0) {
        throw new Error("direct");
    }
    return value;
}

function direct(value: number): number {
    let first = value === 0 ? helper(value) : 0;
    return first + helper(value);
}

function inner(value: number): number {
    if (value !== 0) {
        throw new Error("nested");
    }
    return value;
}

function middle(value: number): number {
    return inner(value);
}

function nested(value: number): number {
    let first = value === 0 ? middle(value) : 0;
    return first + middle(value);
}

function summarizeStack(stack: string): string {
    const frames: string[] = [];
    for (const line of stack.split("\n")) {
        const match = line.match(/\s*at (\S+) \([^)]*:(\d+):\d+\)/);
        if (match) {
            frames.push(match[1] + ":" + match[2]);
        }
    }
    return frames.join("|");
}

for (let index = 0; index < 64; ++index) {
    try {
        direct(index % 2);
    } catch (error) {
    }
    try {
        nested(index % 2);
    } catch (error) {
    }
}

const directCompiled = ArkTools.arkSteedCompileSync(direct);
const nestedCompiled = ArkTools.arkSteedCompileSync(nested);
ArkTools.waitJitCompileFinish(direct);
ArkTools.waitJitCompileFinish(nested);

let directSummary = "";
try {
    direct(1);
} catch (error) {
    directSummary = summarizeStack((error as Error).stack!);
}
let nestedSummary = "";
try {
    nested(1);
} catch (error) {
    nestedSummary = summarizeStack((error as Error).stack!);
}

const directExpected = "helper:33|direct:40|func_main_0:88";
const nestedExpected = "inner:45|middle:51|nested:56|func_main_0:94";
print(directSummary);
print(nestedSummary);
print(directCompiled && nestedCompiled && directSummary === directExpected && nestedSummary === nestedExpected ?
    "PASS" : "FAIL");

function captureStack(): string {
    let stack = "";
    for (let index = 0; index < 1; ++index) {
        stack = new Error("capture").stack!;
    }
    return stack;
}

function smallLeaf(value: number): number {
    return value + 1;
}

function captureCaller(value: number): string {
    let stack = captureStack();
    return stack + "\n" + smallLeaf(value);
}

function captureRoot(value: number): string {
    return captureCaller(value);
}

for (let index = 0; index < 64; ++index) {
    captureRoot(index);
}

const captureCompiled = ArkTools.arkSteedCompileSync(captureRoot);
ArkTools.waitJitCompileFinish(captureRoot);
const captureSummary = summarizeStack(captureRoot(64));
const captureExpected = "captureStack:109|captureCaller:119|captureRoot:124|func_main_0:133";
print(captureSummary);
print(captureCompiled && captureSummary === captureExpected ? "PASS" : "FAIL");
