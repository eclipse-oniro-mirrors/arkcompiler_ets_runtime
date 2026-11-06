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

//! METHOD spreadArrayProtoHole
//! BYTECODE_COUNT starrayspread 1
//! COUNT StoreArraySpreadFast 1
//! COUNT BranchIfStoreArraySpreadAdmitted 1
//! DEOPT_COUNT * 0

declare function print(arg: any): string;
declare const ArkTools: {
    arkSteedCompileSync(args: any): boolean;
    waitJitCompileFinish(args: any): boolean;
};

// An indexed element on Array.prototype invalidates the elements guardians, so
// the runtime guard must route the hole read through the prototype chain.
Array.prototype[1] = 'proto';

function spreadArrayProtoHole(src: number[]): string {
    const dst = [...src];
    return dst[1];
}

print(spreadArrayProtoHole([9, , 7]));
ArkTools.arkSteedCompileSync(spreadArrayProtoHole);
ArkTools.waitJitCompileFinish(spreadArrayProtoHole);
print(spreadArrayProtoHole([9, , 7]));
delete Array.prototype[1];
print(spreadArrayProtoHole([9, , 7]));
