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

/*
 * @tc.name:sharedJSON
 * @tc.desc:test sharedJSON
 * @tc.type: FUNC
 * @tc.require: issueI9K9KB
 */

// @ts-nocheck
declare function print(str: any): string;

const enum BigIntMode {
    DEFAULT = 0,
    PARSE_AS_BIGINT = 1,
    ALWAYS_PARSE_AS_BIGINT = 2,
};

let input = '{"big":1122334455667788999,"small":123,"deci":1234567890.0123456,"shortExp":1.79e+308,"longExp":1.7976931348623157e+308}';
let input2 = '{"zerodeci1":0.0000123,"zerodeci2":0.4799123,"zerodeci3":0.7777334477383838389929292922,"zerodeci4":0.0000000000000123}';

let obj = {
    "innerEntry": {
        "x": 1,
        "y": "abc",
        "str": "innerStr",
    },
    "arr": [1, 2, 3, 4, 5]
}

let arr = [1, 3, 5, 7, 9];

let obj1 = {
    "x": 1,
    "y": "你好"
}

function testJSONParseSendable() {
    print("Start testJSONParseSendable")
    let str = JSON.stringify(obj);
    let sharedObj = JSON.parseSendable(str);
    print("sharedObj.arr: " + sharedObj.arr);
    print("sharedObj.innerEntry: " + sharedObj.innerEntry);
    print("sharedObj.innerEntry.x: " + sharedObj.innerEntry.x);
    print("sharedObj.innerEntry.y: " + sharedObj.innerEntry.y);
    print("sharedObj.innerEntry.str: " + sharedObj.innerEntry.str);

    let str1 = JSON.stringify(arr);
    let sharedArr = JSON.parseSendable(str1);
    print("sharedArr: " + sharedArr);

    let str2 = JSON.stringify(obj1);
    let sharedObj1 = JSON.parseSendable(str2);
    print("sharedObj1.x: " + sharedObj1.x);
    print("sharedObj1.y: " + sharedObj1.y);
}

function jsonRepeatCall() {
    print("Start jsonRepeatCall")
    let stringify1 = JSON.stringify(obj);
    print("stringify1: " + stringify1);
    let sharedObj = JSON.parseSendable(stringify1);
    let stringify2 = JSON.stringify(sharedObj);
    print("stringify2: " + stringify2);
    let normalObj = JSON.parse(stringify2);
    let stringify3 = JSON.stringify(normalObj);
    print("stringify3: " + stringify3);

    let stringify4 = JSON.stringify(arr);
    print("stringify4: " + stringify4);
    let sharedArr = JSON.parseSendable(stringify4);
    let stringify5 = JSON.stringify(sharedArr);
    print("stringify5: " + stringify5);
    let normalArr = JSON.parse(stringify5);
    let stringify6 = JSON.stringify(normalArr);
    print("stringify6: " + stringify6);

    let stringify7 = JSON.stringify(obj1);
    print("stringify7: " + stringify7);
    let sharedObj1 = JSON.parseSendable(stringify7);
    let stringify8 = JSON.stringify(sharedObj1);
    print("stringify8: " + stringify8);
    let normalObj1 = JSON.parse(stringify8);
    let stringify9 = JSON.stringify(normalObj1);
    print("stringify9: " + stringify9);

    let strSendable = '{"x":1, "x":2, "x":3, "x":"你好", "a":4, "x":"你好", "a":2}';
    let sharedObj2 = JSON.parseSendable(strSendable);
    print("sharedObj2.x: " + sharedObj2.x);
    print("sharedObj2.a: " + sharedObj2.a);
    
    let stringify10 = JSON.stringify(sharedObj2);
    print("stringify10: " + stringify10);
}

function testASONBigInt() {
    var options1 = {
        bigIntMode: BigIntMode.PARSE_AS_BIGINT,
    }
    var obj = JSON.parseSendable(input, undefined, options1);

    print(obj.small);
    print(obj.big);
    print((typeof obj.big === "bigint"));

    var options2 = {
        bigIntMode: BigIntMode.ALWAYS_PARSE_AS_BIGINT,
    }
    var obj2 = JSON.parseSendable(input, undefined, options2);
    print(obj2.small);
    print(obj2.big);
    print((typeof obj2.small === "bigint"));

    var options3 = {
        bigIntMode: BigIntMode.PARSE_AS_BIGINT,
    }
    var obj3 = JSON.parseSendable(input, undefined, options3);
    print(obj3.deci);
    print(obj3.shortExp);
    print(obj3.longExp);

    var options4 = {
        bigIntMode: BigIntMode.PARSE_AS_BIGINT,
    }
    var obj4 = JSON.parseSendable(input, undefined, options4);
    var output = JSON.stringifySendable(obj4);
    print(output);

    var options5 = {
        bigIntMode: BigIntMode.ALWAYS_PARSE_AS_BIGINT,
    }
    var obj5 = JSON.parseSendable(input, undefined, options5);
    var output2 = JSON.stringifySendable(obj5);
    print(output2);
}

function testJSONBigInt() {
    var options1 = {
        bigIntMode: BigIntMode.PARSE_AS_BIGINT,
    }
    var obj = JSON.parseBigInt(input, undefined, options1);

    print(obj.small);
    print(obj.big);
    print((typeof obj.big === "bigint"));

    var options2 = {
        bigIntMode: BigIntMode.ALWAYS_PARSE_AS_BIGINT,
    }
    var obj2 = JSON.parseBigInt(input, undefined, options2);
    print(obj2.small);
    print(obj2.big);
    print((typeof obj2.small === "bigint"));

    var options3 = {
        bigIntMode: BigIntMode.PARSE_AS_BIGINT,
    }
    var obj3 = JSON.parseBigInt(input, undefined, options3);
    print(obj3.deci);
    print(obj3.shortExp);
    print(obj3.longExp);

    var options4 = {
        bigIntMode: BigIntMode.PARSE_AS_BIGINT,
    }
    var obj4 = JSON.parseBigInt(input, undefined, options4);
    var output = JSON.stringifyBigInt(obj4);
    print(output);

    var options5 = {
        bigIntMode: BigIntMode.ALWAYS_PARSE_AS_BIGINT,
    }
    var obj5 = JSON.parseBigInt(input, undefined, options5);
    var output2 = JSON.stringifyBigInt(obj5);
    print(output2);

}

function testJSONNormal() {
    var obj = JSON.parseBigInt(input, undefined, undefined);
    print(obj.small);
    print(obj.big);
    print(obj.deci);
    print(obj.shortExp);
    print(obj.longExp);
    print((typeof obj.small === "number"));
    print((typeof obj.big === "number"));
    print((typeof obj.deci === "number"));
    print((typeof obj.shortExp === "number"));
    print((typeof obj.longExp === "number"));
}

function testJSONreviver() {
    let opt = {
        bigIntMode: BigIntMode.ALWAYS_PARSE_AS_BIGINT,
    }
    try {
        JSON.parseSendable(input, function (k, v) {return v;});
    } catch (error) {
        print(error);
    }
    try {
        JSON.parseSendable(input, function (k, v) {return v;}, opt);
    } catch (error) {
        print(error);
    }
}

function testJSONZeroDeci() {
    let opt1 = {
        bigIntMode: BigIntMode.ALWAYS_PARSE_AS_BIGINT,
    }
    let opt2 = {
        bigIntMode: BigIntMode.PARSE_AS_BIGINT,
    }
    let obj1 = JSON.parseSendable(input2, undefined, opt1);
    print(obj1.zerodeci1);
    print(obj1.zerodeci2);
    print(obj1.zerodeci3);
    print(obj1.zerodeci4);
    let obj2 = JSON.parseSendable(input2, undefined, opt2);
    print(obj2.zerodeci1);
    print(obj2.zerodeci2);
    print(obj2.zerodeci3);
    print(obj2.zerodeci4);
    let obj3 = JSON.parseBigInt(input2, undefined, opt1);
    print(obj3.zerodeci1);
    print(obj3.zerodeci2);
    print(obj3.zerodeci3);
    print(obj3.zerodeci4);
    let obj4 = JSON.parseBigInt(input2, undefined, opt2);
    print(obj4.zerodeci1);
    print(obj4.zerodeci2);
    print(obj4.zerodeci3);
    print(obj4.zerodeci4);
}

testJSONParseSendable();
jsonRepeatCall();
testASONBigInt();
testJSONBigInt();
testJSONNormal();
testJSONreviver();
testJSONZeroDeci();