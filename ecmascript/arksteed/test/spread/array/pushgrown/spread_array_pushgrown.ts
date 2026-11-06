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
 * Push-grown sources: the backing length is the capacity, so the slots
 * between the array length and the capacity hold HOLE filler that the copy
 * never reads — the scan must bound itself by the array length (min of the
 * two), or every push-built array is demoted to the runtime slow path.
 * The grown-length source keeps the array length within the backing
 * (SetLength grows the backing to the new length), and the holes the
 * growth leaves behind are rejected by the scan itself. Its differently
 * shaped result deopts the shape-specialized code eagerly (expected).
 */

//! METHOD spreadArrayPushGrown
//! BYTECODE_COUNT starrayspread 1
//! COUNT BranchIfStoreArraySpreadAdmitted 1
//! DEOPT_COUNT Deoptimize* 1

declare function print(arg: any): string;
declare const ArkTools: {
    arkSteedCompileSync(args: any): boolean;
    waitJitCompileFinish(args: any): boolean;
};

function spreadArrayPushGrown(src: number[]): string {
    const dst = [...src];
    return dst.length + ':' + dst[0] + ',' + dst[dst.length - 1];
}

const grown: number[] = [];
for (let i = 0; i < 12; i++) {
    grown.push(i + 1);
}

print(spreadArrayPushGrown(grown));
ArkTools.arkSteedCompileSync(spreadArrayPushGrown);
ArkTools.waitJitCompileFinish(spreadArrayPushGrown);
print(spreadArrayPushGrown(grown));

const sparse: number[] = [1, 2, 3];
sparse.length = 5;
print(spreadArrayPushGrown(sparse));
