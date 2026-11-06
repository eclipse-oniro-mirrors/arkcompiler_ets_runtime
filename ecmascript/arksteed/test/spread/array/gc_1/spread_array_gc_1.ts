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

//! PARAMS --asm-interpreter=true --compiler-enable-baselinejit=false --enable-force-gc=true
//! METHOD spreadArrayGc
//! BYTECODE_COUNT starrayspread 1
//! COUNT StoreArraySpreadFast 2
//! COUNT BranchIfStoreArraySpreadAdmitted 2
//! COUNT JumpLoop 1
//! DEOPT_COUNT * 0
//! GC_COUNT_GE * 1

declare function print(arg: any): string;
declare const ArkTools: {
    arkSteedCompileSync(args: any): boolean;
    waitJitCompileFinish(args: any): boolean;
};

function spreadArrayGc(src: number[]): number {
    let acc = 0;
    for (let i = 0; i < 20000; i++) {
        const dst = [...src];
        acc += dst.length + dst[0];
    }
    return acc;
}

ArkTools.arkSteedCompileSync(spreadArrayGc);
ArkTools.waitJitCompileFinish(spreadArrayGc);
print(spreadArrayGc([11, 22, 33]));
