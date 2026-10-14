/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

// G10.1 additive evidence (A2, no weakened assertion): the compiled `f`
// contains the checked `x++`, and the post-installation call with an
// out-of-int32 operand leaves the generated code instead of silently
// continuing in the interpreter.

//! METHOD f
//! HAS I32IncWithOverflow
//! COMPILE_COUNT_GE *f* 1
//! EXEC_COUNT_GE *f* 1
//! DEOPT_COUNT_GE *Deoptimize* 1

function f(x) {
    if (x++ < -1) {
        x++;
        print(x)
    }
}

f(-100);
f(12345678912345);

ArkTools.arkSteedCompileSync(f);
print(true);

f(-12345678912345);