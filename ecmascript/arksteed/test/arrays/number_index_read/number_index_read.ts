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

// @ts-nocheck

//! PARAMS --compiler-jit-hotness-threshold=60000

//! DEOPT_COUNT *Deoptimize* 0
//! METHOD numberIndexRead
//! HAS LoadTaggedElement
//! HAS F64ToI32Trunc
//! HAS I32ToF64
//! HAS_NOT CheckedTaggedIntToI32
//! COMPILE_COUNT numberIndexRead 1
//! EXEC_COUNT_GE *numberIndexRead 3
function numberIndexRead(array, key) {
    return array[key];
}
const values = ['zero', 'one', 'two', 'three'];
for (let i = 0; i < 64; i++) numberIndexRead(values, 1);
ArkTools.arkSteedCompileSync(numberIndexRead);
print(numberIndexRead(values, Math.sqrt(4)));
print(numberIndexRead(values, Math.sqrt(9)));
print(numberIndexRead(values, -0));
print(ArkTools.arkSteedIsCompiled(numberIndexRead));
ArkTools.forceFullGC();
print(numberIndexRead(values, Math.sqrt(1)));
print('OK');
