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
 * @tc.name:sendableirnewsenv
 * @tc.desc: module whose init executes NEWSENDABLEENV (imm8) and STSENDABLEVAR imm4
 * @tc.type: FUNC
 */

// three module scope sendable functions, all captured by touch, so module init
// executes: callruntime.newsendableenv 0x3 (imm8) + stsendablevar imm4 x3
function nsf0() {
    "use sendable";
    return 10;
}

function nsf1() {
    "use sendable";
    return 20;
}

function nsf2() {
    "use sendable";
    return 30;
}

function touch(): number {
    return nsf0() + nsf1() + nsf2();
}

export function newsenvCheck(): number {
    return touch();
}
