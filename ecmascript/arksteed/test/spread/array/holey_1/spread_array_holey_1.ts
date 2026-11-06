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

//! METHOD spreadArrayHoley
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

function spreadArrayHoley(src: number[]): string {
    const dst = [...src];
    return dst.length + ':' + dst[0] + ',' + dst[1] + ',' + dst[3] + ',' + dst[4];
}

print(spreadArrayHoley([1, , 3, , 5]));
ArkTools.arkSteedCompileSync(spreadArrayHoley);
ArkTools.waitJitCompileFinish(spreadArrayHoley);
print(spreadArrayHoley([1, , 3, , 5]));
print(spreadArrayHoley([6, , 7, , 8]));
