/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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
declare function assert_equal(a: Object, b: Object):void;

function tryHello(v: number): void {
    let a: number = 1;
    let ret: number = a + v;
    assert_equal(ret, "1a");
}
assert_equal(ArkTools.checkDeoptStatus(tryHello, false), true);
tryHello(<number><Object>'a');
assert_equal(ArkTools.checkDeoptStatus(tryHello, true), true);
for (let i = 0; i < 25; i++) {
    tryHello(<number><Object>'a');
}
assert_equal(ArkTools.checkDeoptStatus(tryHello, true), true);

function tryIf(v: number, b: number): void {
    let a : number = 1;

    if (b == 1) {
        let ret: number = a + v;
        assert_equal(ret, "1a");
    }
}
tryIf(<number><Object>'a', 1);

function tryPhi(v: number, b: number): void {
    let a : number = 1;
    let ret: number = 1;
    if (b == 1) {
        ret = a + v;
    }
    assert_equal(ret, "1a");
}

tryPhi(<number><Object>'a', 1);

function tryLoop(v: number, b: number): void {
    let a : number = 1;
    let ret: number = 1;

    for (var i = 0; i < b; i++) {
        ret = a + v;
    }
    assert_equal(ret, "1a");
}

tryLoop(<number><Object>'a', 1);
