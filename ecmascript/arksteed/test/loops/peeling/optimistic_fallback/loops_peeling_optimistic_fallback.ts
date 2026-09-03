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

//! METHOD peeling_optimistic_fallback
//! COUNT CallCommonStub Less 3
//! COUNT JumpLoop 1

function peeling_optimistic_fallback(n)
{
    let value = 0;
    for (let i = 0; i < n; i++) {
        value = i === 0 ? 1 : 1.5;
    }
    return value;
}

ArkTools.arkSteedCompileSync(peeling_optimistic_fallback);

print(peeling_optimistic_fallback(0));
print(peeling_optimistic_fallback(1));
print(peeling_optimistic_fallback(3));
