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

//! METHOD spreadArrayMulti
//! BYTECODE_COUNT starrayspread 2
//! COUNT StoreArraySpreadFast 1
//! COUNT BranchIfStoreArraySpreadAdmitted 1
//! COUNT CallRuntime StArraySpread 2
//! DEOPT_COUNT * 0

declare function print(arg: any): string;
declare const ArkTools: {
    arkSteedCompileSync(args: any): boolean;
    waitJitCompileFinish(args: any): boolean;
};

// The first spread starts at constant index 0 and takes the inline path; the
// second spread's running index is dynamic and keeps the pure stub lowering.
function spreadArrayMulti(a: number[], b: number[]): string {
    const dst = [...a, 900, ...b];
    return dst.join(',');
}

print(spreadArrayMulti([1, 2], [3, 4, 5]));
ArkTools.arkSteedCompileSync(spreadArrayMulti);
ArkTools.waitJitCompileFinish(spreadArrayMulti);
print(spreadArrayMulti([1, 2], [3, 4, 5]));
print(spreadArrayMulti([], [7]));
