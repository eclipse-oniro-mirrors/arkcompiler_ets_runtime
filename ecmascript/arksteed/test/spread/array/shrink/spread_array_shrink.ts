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
 * Shrunk source: cutting the length down keeps the backing capacity, so
 * the slots past the new length hold HOLE filler while the live range
 * stays dense — the scan bounds itself by the array length and admits,
 * and the copy stops at the array length.
 */

//! METHOD spreadArrayShrink
//! BYTECODE_COUNT starrayspread 1
//! COUNT BranchIfStoreArraySpreadAdmitted 1
//! DEOPT_COUNT * 0

declare function print(arg: any): string;
declare const ArkTools: {
    arkSteedCompileSync(args: any): boolean;
    waitJitCompileFinish(args: any): boolean;
};

function spreadArrayShrink(src: number[]): string {
    const dst = [...src];
    return dst.length + ':' + dst[0] + ',' + dst[dst.length - 1];
}

const shrunk: number[] = [1, 2, 3];
shrunk.length = 2;

print(spreadArrayShrink(shrunk));
ArkTools.arkSteedCompileSync(spreadArrayShrink);
ArkTools.waitJitCompileFinish(spreadArrayShrink);
print(spreadArrayShrink(shrunk));
