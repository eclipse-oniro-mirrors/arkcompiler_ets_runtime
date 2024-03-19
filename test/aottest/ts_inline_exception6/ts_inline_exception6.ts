/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

declare function print(arg:any):string;
declare function assert_equal(a: Object, b: Object):void;
declare function assert_unreachable():void;
declare class ArkTools {
    static hiddenStackSourceFile(): boolean;
}
ArkTools.hiddenStackSourceFile()

try {
    function foo1() {
        c
    }
    function foo2() {
        foo1()
    }
    function foo3(a: number): number {
        let b: number = a + 1
        foo2()
    }
    function foo4() {
        foo3(<number><Object>'a', 1)
    }
    function foo5() {
        foo4(1)
    }
    function foo6() {
        foo5()
    }
    foo6(1)
    assert_unreachable()
} catch (e) {
    assert_equal(e.message, "c is not defined")
    print(e.stack)
}