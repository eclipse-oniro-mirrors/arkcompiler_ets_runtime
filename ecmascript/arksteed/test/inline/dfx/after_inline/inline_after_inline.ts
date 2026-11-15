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

//! PARAMS --compiler-arksteed-source-position-table=true --compiler-arksteed-enable-inline=true --compiler-min-inline-call-frequency=0.5 --compiler-max-inline-bytecodes=128 --compiler-arksteed-max-cumulative-inline-bytecode-size=256 --compiler-arksteed-max-graph-nodes-before-inline=1000

//! METHOD afterInline
//! HAS_NOT CallKnownJSFunction
//! HAS_NOT Call

declare function print(arg: any): string;

function inlineReturn(value: number): number {
    return value + 1;
}

function afterInline(): void {
    inlineReturn(1);
    throw new Error("after");
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

for (let i = 0; i < 64; i++) {
    try {
        afterInline();
    } catch (e) {
    }
}

const compiled = ArkTools.arkSteedCompileSync(afterInline);
ArkTools.waitJitCompileFinish(afterInline);

let stack = "";
try {
    afterInline();
} catch (e) {
    const err = e as Error;
    stack = err.stack!;
}

const summary = summarizeStack(stack);
const expected = "afterInline:30|func_main_0:56";
print(summary);
print(compiled && summary === expected ? "PASS" : "FAIL");
