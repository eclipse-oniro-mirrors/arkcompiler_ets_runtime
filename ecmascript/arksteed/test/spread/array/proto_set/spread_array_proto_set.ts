/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * A hole in the spread source resolves through the prototype chain above
 * Array.prototype, so the runtime guard must reject holey backings: none of
 * the prototype-set spellings below invalidates the elements guardians
 * (SetPrototypeTransition only notifies for JSArray receivers), which makes
 * the hole-to-undefined mapping of the fast copy unsound for holey inputs.
 */

//! METHOD spreadArrayProtoSet
//! BYTECODE_COUNT starrayspread 1
//! COUNT BranchIfStoreArraySpreadAdmitted 1
//! DEOPT_COUNT * 0

//! METHOD spreadArrayProtoSetDeep
//! BYTECODE_COUNT starrayspread 1
//! COUNT BranchIfStoreArraySpreadAdmitted 1
//! DEOPT_COUNT * 0

declare function print(arg: any): string;
declare const ArkTools: {
    arkSteedCompileSync(args: any): boolean;
    waitJitCompileFinish(args: any): boolean;
};

function spreadArrayProtoSet(src: number[]): string {
    const dst = [...src];
    return dst[0] + ',' + dst[1] + ',' + dst[2];
}

function spreadArrayProtoSetDeep(src: number[]): string {
    const dst = [...src];
    return dst[0] + ',' + dst[1] + ',' + dst[2];
}

print(spreadArrayProtoSet([9, , 5]));
ArkTools.arkSteedCompileSync(spreadArrayProtoSet);
ArkTools.waitJitCompileFinish(spreadArrayProtoSet);
print(spreadArrayProtoSet([9, , 5]));
print(spreadArrayProtoSet([1, 2, 3]));

const np = Object.create(null);
np[1] = 'NP';
Object.setPrototypeOf(Object.prototype, np);
print(spreadArrayProtoSet([9, , 5]));
print(spreadArrayProtoSet([1, 2, 3]));

const np2 = Object.create(null);
np2[2] = 'NP2';
Object.prototype.__proto__ = np2;
print(spreadArrayProtoSet([9, 5, , 7]));

const mid = Object.create(Object.create(null));
mid[1] = 'MID';
ArkTools.arkSteedCompileSync(spreadArrayProtoSetDeep);
ArkTools.waitJitCompileFinish(spreadArrayProtoSetDeep);
Object.setPrototypeOf(Object.prototype, mid);
print(spreadArrayProtoSetDeep([9, , 5]));
