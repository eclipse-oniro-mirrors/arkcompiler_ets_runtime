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
 * Push-grown source with a real hole at the last live index under a
 * polluted prototype chain: the backing capacity sits above the array
 * length, so the scan's bound (the array length) must skip the capacity
 * filler and still reach the hole — the fast copy would map it to
 * undefined where the chain above Array.prototype maps it to NP. The
 * dense push-grown control must stay admitted under the same pollution.
 */

//! METHOD spreadArrayPushGrownProto
//! BYTECODE_COUNT starrayspread 1
//! COUNT BranchIfStoreArraySpreadAdmitted 1
//! DEOPT_COUNT * 0

declare function print(arg: any): string;
declare const ArkTools: {
    arkSteedCompileSync(args: any): boolean;
    waitJitCompileFinish(args: any): boolean;
};

function spreadArrayPushGrownProto(src: number[]): string {
    const dst = [...src];
    return dst.length + ':' + dst[dst.length - 1];
}

const holey: number[] = [];
for (let i = 0; i < 12; i++) {
    holey.push(i + 1);
}
holey.length = 13;

const dense: number[] = [];
for (let i = 0; i < 12; i++) {
    dense.push(i + 1);
}

print(spreadArrayPushGrownProto(holey));
ArkTools.arkSteedCompileSync(spreadArrayPushGrownProto);
ArkTools.waitJitCompileFinish(spreadArrayPushGrownProto);
print(spreadArrayPushGrownProto(holey));

const np = Object.create(null);
np[12] = 'NP';
Object.setPrototypeOf(Object.prototype, np);
print(spreadArrayPushGrownProto(holey));
print(spreadArrayPushGrownProto(dense));
