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

//! METHOD spreadArrayFast
//! BYTECODE_COUNT starrayspread 1
//! COUNT StoreArraySpreadFast 1
//! COUNT BranchIfStoreArraySpreadAdmitted 1
//! COUNT CallRuntime StArraySpread 1
//! DEOPT_COUNT * 0

declare function print(arg: any): string;
declare const ArkTools: {
    arkSteedCompileSync(args: any): boolean;
    waitJitCompileFinish(args: any): boolean;
};

function spreadArrayFast(src: number[]): number {
    const dst = [...src];
    return dst.length * 100 + dst[0] + dst[2];
}

print(spreadArrayFast([1, 2, 3]));
ArkTools.arkSteedCompileSync(spreadArrayFast);
ArkTools.waitJitCompileFinish(spreadArrayFast);
print(spreadArrayFast([4, 5, 6]));
print(spreadArrayFast([7, 8, 9, 10]));
