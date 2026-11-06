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
 * Double-elements source: an unboxed NUMBER backing fails the guard's
 * backing-kind check and takes the runtime slow path; an admission leak
 * here would copy raw unboxed doubles as tagged.
 */

//! METHOD spreadArrayDouble
//! BYTECODE_COUNT starrayspread 1
//! COUNT BranchIfStoreArraySpreadAdmitted 1
//! DEOPT_COUNT * 0

declare function print(arg: any): string;
declare const ArkTools: {
    arkSteedCompileSync(args: any): boolean;
    waitJitCompileFinish(args: any): boolean;
};

function spreadArrayDouble(src: number[]): string {
    const dst = [...src];
    return dst.length + ':' + dst[0] + ',' + dst[dst.length - 1];
}

const dbl: number[] = [1.5, 2.5];

print(spreadArrayDouble(dbl));
ArkTools.arkSteedCompileSync(spreadArrayDouble);
ArkTools.waitJitCompileFinish(spreadArrayDouble);
print(spreadArrayDouble(dbl));
