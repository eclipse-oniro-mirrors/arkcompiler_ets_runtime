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

//! METHOD spreadArrayLong
//! BYTECODE_COUNT starrayspread 1
//! COUNT StoreArraySpreadFast 1
//! COUNT BranchIfStoreArraySpreadAdmitted 1
//! DEOPT_COUNT * 0

declare function print(arg: any): string;
declare const ArkTools: {
    arkSteedCompileSync(args: any): boolean;
    waitJitCompileFinish(args: any): boolean;
};

const longSrc: number[] = [];
for (let i = 0; i < 150; i++) {
    longSrc.push(i);
}

// The backing length exceeds the fast-spread cap, so the guard must keep this
// spread on the runtime slow path.
function spreadArrayLong(src: number[]): number {
    const dst = [...src];
    return dst.length * 1000 + dst[99] + dst[149];
}

print(spreadArrayLong(longSrc));
ArkTools.arkSteedCompileSync(spreadArrayLong);
ArkTools.waitJitCompileFinish(spreadArrayLong);
print(spreadArrayLong(longSrc));
