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
// G11.1 additive evidence: the warmed site resolves the element access for one
// key kind; the second observed call passes a string key, the compiled key
// check fails and the eager transition resumes the interpreter with the same
// value. The leaf therefore carries its actual transition direction, not an
// arbitrary zero-deopt assertion (V1).
//! METHOD test
//! HAS LoadTaggedField
//! COMPILE_COUNT_GE *test* 1
//! EXEC_COUNT_GE *test* 1
//! DEOPT_COUNT_GE *Deoptimize* 1

function test(str, index) {
    return str[index] == '0';
}

let str = "adas0s";
for (let i = 0; i < 1; i++) {
    test(str, 4);
    test(str, '4');
}

ArkTools.arkSteedCompileSync(test);
print(true);
print(test(str, 4));
print(test(str, '4'));
