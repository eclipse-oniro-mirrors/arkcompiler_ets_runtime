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

/*
 * @tc.name:asmstackoverflow
 * @tc.desc:test stack overflow in asm
 * @tc.type: FUNC
 * @tc.require: issueI5NO8G
 */
function foo(x,y,z,a,b)
{
    foo(x,y,z,a,b)
}
try {
    foo(1, 2, 3, 4, 5)
} catch (e) {
    if ((e instanceof RangeError)) {
        print("stack overflow2!");
    }
}

function foo2()
{
    foo2()
}
try {
    foo2()
} catch (e) {
    if ((e instanceof RangeError)) {
        print("stack overflow!");
    }
}

const obj = {};
const pro = new Proxy({}, obj);
obj.__proto__ = pro;
try {
    obj[10];
} catch (e) {
    if (e instanceof RangeError) {
        print("proxy stackoverflow!");
    }
}
try {
    function func() { }
    Function.prototype.__proto__ = new Proxy(func, {})
    Function.prototype.__proto__ = new Proxy(func, {})
} catch (error) {
    if (error instanceof RangeError) {
        print("proxy stackoverflow2!");
    }
}

// callarg0-3 callrange
function callarg0() {
    callarg0();
}
try {
    callarg0();
} catch (e) {
    print("callarg0 stackoverflow");
}

function callarg1(a) {
    callarg1(a);
}
try {
    callarg1(1);
} catch (e) {
    print("callarg1 stackoverflow");
}

function callarg2(a, b) {
    callarg2(a, b);
}
try {
    callarg2(1, 2);
} catch (e) {
    print("callarg2 stackoverflow");
}

function callarg3(a, b, c) {
    callarg3(a, b, c);
}
try {
    callarg3(1, 2, 3);
} catch (e) {
    print("callarg3 stackoverflow");
}

function callrange(a, b, c, d) {
    callrange(a, b, c, d);
}
try {
    callrange(1, 2, 3, 4);
} catch (e) {
    print("callrange stackoverflow");
}

// callthis0-3 callthisrange
var obj2 = {
    callthis0: function () {
        this.callthis0();
    },
    callthis1: function (a) {
        this.callthis1(a);
    },
    callthis2: function (a, b) {
        this.callthis2(a, b);
    },
    callthis3: function (a, b, c) {
        this.callthis3(a, b, c);
    },
    callthisrange: function (a, b, c, d) {
        this.callthisrange(a, b, c, d);
    },
}
try {
    obj2.callthis0();
} catch (e) {
    print("callthis0 stackoverflow");
}
try {
    obj2.callthis1(1);
} catch (e) {
    print("callthis1 stackoverflow");
}
try {
    obj2.callthis2(1, 2);
} catch (e) {
    print("callthis2 stackoverflow");
}
try {
    obj2.callthis3(1, 2, 3);
} catch (e) {
    print("callthis3 stackoverflow");
}
try {
    obj2.callthisrange(1, 2, 3, 4);
} catch (e) {
    print("callthisrange stackoverflow");
}

// callstatic
class MyClass {
    static callstatic0() {
        MyClass.callstatic0();
    }
    static callstatic1(a) {
        MyClass.callstatic1(a);
    }
    static callstatic2(a, b) {
        MyClass.callstatic2(a, b);
    }
    static callstatic3(a, b, c) {
        MyClass.callstatic3(a, b, c);
    }
    static callstaticrange(a, b, c, d) {
        MyClass.callstaticrange(a, b, c, d);
    }
}
try {
    MyClass.callstatic0();
} catch (e) {
    print("callstatic0 stackoverflow");
}
try {
    MyClass.callstatic1(1);
} catch (e) {
    print("callstatic1 stackoverflow");
}
try {
    MyClass.callstatic2(1, 2);
} catch (e) {
    print("callstatic2 stackoverflow");
}
try {
    MyClass.callstatic3(1, 2, 3);
} catch (e) {
    print("callstatic3 stackoverflow");
}
try {
    MyClass.callstaticrange(1, 2, 3, 4);
} catch (e) {
    print("callstaticrange stackoverflow");
}

// callnew
class MyClass2 {
    constructor() {
        new MyClass2();
    }
}
try {
    new MyClass2();
} catch (e) {
    print("callnew stackoverflow");
}

class MyClass3 {
    constructor(a, b, c, d) {
        new MyClass3(a, b, c, d);
    }
}
try {
    new MyClass3(1, 2, 3, 4);
} catch (e) {
    print("callnew stackoverflow2");
}