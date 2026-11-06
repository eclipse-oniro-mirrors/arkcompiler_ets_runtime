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

/*
 * Regression for the admission hole-scan's register lifetime: the scan
 * index must survive the element load. With the index and the loaded
 * value sharing a register, a trailing Smi zero loads as the raw word 0
 * and reads as the loop's end condition — the scan stops after one slot,
 * mis-admits the holey array, and the copy then either maps the surviving
 * hole to undefined or stores it raw into the result. hasOwnProperty(1)
 * separates the three outcomes: the runtime path materializes a dense
 * slot (true), a mapped undefined is also dense (true, but the polluted
 * line below then reads undefined instead of NP), and a raw copied hole
 * leaves no own property (false). The hole must reject to the runtime
 * path, which resolves it through the prototype chain. The pollution also
 * invalidates the compiled frame's known-builtin cell for the
 * hasOwnProperty call, so exactly one lazy deopt is expected.
 */

//! METHOD spreadArrayScanZero
//! BYTECODE_COUNT starrayspread 1
//! COUNT BranchIfStoreArraySpreadAdmitted 1
//! DEOPT_COUNT * 1

declare function print(arg: any): string;
declare const ArkTools: {
    arkSteedCompileSync(args: any): boolean;
    waitJitCompileFinish(args: any): boolean;
};

function spreadArrayScanZero(src: number[]): string {
    const dst = [...src];
    return dst.length + ':' + dst[1] + ':' + dst.hasOwnProperty(1);
}

print(spreadArrayScanZero([9, , 0]));
ArkTools.arkSteedCompileSync(spreadArrayScanZero);
ArkTools.waitJitCompileFinish(spreadArrayScanZero);
print(spreadArrayScanZero([9, , 0]));
print(spreadArrayScanZero([1, 2, 3]));

const np = Object.create(null);
np[1] = 'NP';
Object.setPrototypeOf(Object.prototype, np);
print(spreadArrayScanZero([9, , 0]));
