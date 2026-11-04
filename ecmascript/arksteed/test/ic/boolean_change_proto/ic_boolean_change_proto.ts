/*
 * Copyright (c) 2025 Huawei Device Co., Ltd.
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

// @ts-nocheck
//! PARAMS --compiler-jit-hotness-threshold=60000
declare function print(arg: any): string;

let a = true;

function visit(obj) {
    return obj.valueOf;
}

Boolean.prototype.valueOf = "Boolean.ValueOf"

for (let i = 0; i < 1000; i++) {
    visit(a);
}
ArkTools.arkSteedCompileSync(visit);

// G11.2 auxiliary poly candidate: the site is warmed with the boolean
// primitive and installed before the prototype property is deleted and
// rebound. The named load of this case is served by the generic property stub
// rather than a resolved HClass set, so the prototype change after
// installation produces no deopt event; the compilation/entry counters still
// prove that the installed code of the target ran.
//! COMPILE_COUNT_GE *visit* 1
//! EXEC_COUNT_GE *visit* 1
//! DEOPT_COUNT *Deoptimize* 0

print(true);
print(visit(a));

delete Boolean.prototype.valueOf;
print(visit(a));

Boolean.prototype.valueOf = "boolean.valueOf"
print(visit(a))
