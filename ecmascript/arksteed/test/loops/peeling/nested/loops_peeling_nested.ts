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

//! METHOD peeling_nested
//! COUNT CallCommonStub Less 3
//! COUNT JumpLoop 2

function peeling_nested(outerLimit, innerLimit)
{
    let sum = 0;
    for (let i = 0; i < outerLimit; i++) {
        for (let j = 0; j < innerLimit; j++) {
            sum += i * 10 + j;
        }
    }
    return sum;
}

ArkTools.arkSteedCompileSync(peeling_nested);

print(peeling_nested(0, 3));
print(peeling_nested(2, 0));
print(peeling_nested(2, 3));
