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
//mjsunit/regress/regress-3135.js
// Properties are serialized once.
print(JSON.stringify({ x : 1 }, ["x", 1, "x", 1]));
print(JSON.stringify({ 1 : 1 }, ["x", 1, "x", 1]));
print(JSON.stringify({ 1 : 1 }, ["1", 1, "1", 1]));
print(JSON.stringify({ 1 : 1 }, [1, "1", 1, "1"]));
// Properties are visited at most once.
var fired = 0;
var getter_obj = { get x() { fired++; return 2; } };
print(JSON.stringify(getter_obj, ["x", "y", "x"]));
print(1 == fired);
// Order of the replacer array is followed.
print(JSON.stringify({ x : 3, y : 4}, ["y", "x"]));
print(JSON.stringify({ x : 3, y : 4, 1 : 2 }, ["y", 1, "x"]));
// With a replacer array the value of the property is retrieved using [[Get]]
// ignoring own and enumerability.
var a = { x : 8 };
print(JSON.stringify(a, ["__proto__", "x", "__proto__"]));
a.__proto__ = { x : 7 };
print(JSON.stringify(a, ["__proto__", "x"]));
var b = { __proto__: { x: 9 } };
print(JSON.stringify(b));
print(JSON.stringify(b, ["x"]));
var c = {x: 10};
Object.defineProperty(c, 'x', { enumerable: false });
print(JSON.stringify(c));
print(JSON.stringify(c, ["x"]));
// Arrays are not affected by the replacer array.
print(JSON.stringify([9, 8, 7], [1, 1]));
var mixed_arr = [11,12,13];
mixed_arr.x = 10;
print(JSON.stringify(mixed_arr, [1, 0, 1]));
// Array elements of objects are affected.
var mixed_obj = { x : 3 };
mixed_obj[0] = 6;
mixed_obj[1] = 5;
print(JSON.stringify(mixed_obj, [1, 0, 1]));
// Nested object.
print(JSON.stringify({ x: 1, y:2, z: {x:3, b:4}}, ["z","x"]));
// Objects in the replacer array are ignored.
print(JSON.stringify({ x : 1, "1": 1 }, [{}]));
print(JSON.stringify({ x : 1, "1": 1 }, [true, undefined, null]));
print(JSON.stringify({ x : 1, "1": 1 }, [{ toString: function() { return "x";} }]));
print(JSON.stringify({ x : 1, "1": 1 }, [{ valueOf: function() { return 1;} }]));
// Make sure that property names that clash with the names of Object.prototype
// still works.
print(JSON.stringify({ toString: 42 }, ["toString"]));
// Number wrappers and String wrappers should be unwrapped.
print(JSON.stringify({ 1: 1, s: "s" },[new Number(1), new String("s")]));
// bigint/regress-minuszero.js
print(BigInt("-0 ") == -0);
print("-0 " == 0n);
print(BigInt("-0") == -0);
print(-0n == -0);
print(-0n == 0n);

print(BigInt("-0 ") > -1);
print("-0 " > -1n);
print(BigInt("-0") > -1);
print(-0n > -1);

print(BigInt("-0 ") & 1n);
print(BigInt("-0") & 1n);
print(-0n & 1n);
var zero = BigInt("-0 ");
print(++zero == 1n);
zero = BigInt("-0");
print(++zero  == 1n);
zero = -0n;
print(++zero  == 1n);
// mjsunit/regress/regress-5974.js
(function() {
    var a = Array(...Array(5)).map(() => 1);
    print(a);
})();
//mjsunit/regress/regress-416.js
print(isNaN(new Date(1e81).getTime()));
print(isNaN(new Date(1e81).getTime()));
print(isNaN(new Date(-1e81).getTime()));
print(isNaN(new Date(1e81, "").getTime()));
print(isNaN(new Date(-1e81, "").getTime()));
print(isNaN(new Date(Number.NaN).getTime()));
print(isNaN(new Date(Number.NaN, "").getTime()));

function assertArrayEquals(expected, found) {
    if (expected.length != found.length) {
        return false
    } else{
        for (var i = 0; i < expected.length; ++i) {
            if (expected[i] != found[i]) {
                return false
            }
        }
        return true
    }
};

// regress/regress-crbug-658691.js
function f(a, b, c) {
    "use strict";
    return Reflect.set({});
  }
  
  // The {g} function is compiled using Ignition.
  // 1) The call to {f} requires arguments adaptation.
  // 2) The call to {f} is not in tail position.
  ;
  ArkTools.prepareFunctionForOptimization(f);
  function g() {
    return f() + '-no-tail';
  }
  
  print(g());
  ArkTools.optimizeFunctionOnNextCall(f);
  // regress/regress-4654.js
  print(g());
  print('hello\u0000foobar' == 'hello\u0000foobar'.normalize('NFC'));

// regress/regress-6223.js
var ab = new Int8Array(20).map((v, i) => i).buffer;
var ta = new Int8Array(ab, 0, 10);
var seen_length = -1;
ta.constructor = {
  [Symbol.species]: function(len) {
    seen_length = len;
    return new Int8Array(ab, 1, len);
  }
};

print(-1 == seen_length);
print(assertArrayEquals([0,1,2,3,4,5,6,7,8,9], ta));
var tb = ta.slice();
print(10 == seen_length);
print(assertArrayEquals([0,0,0,0,0,0,0,0,0,0], ta));
print(assertArrayEquals([0,0,0,0,0,0,0,0,0,0], tb));
// regress/regress-crbug-1404820.js
(function array_iterator() {
    let array_iterator_prototype = [].values().__proto__;
    let iter;
    array_iterator_prototype.return = function(value) {
      iter = this;
      return {value: value, done: true};
    };
  
    let array = [["good1"], ["good2"], "bad", "next", 5, 6, 7, 8];
  
    // Aborted iteration in a builtin.
    try {
      new WeakSet(array);
    } catch (e) {}
    // iter points at "bad" item, so next() must return "next" value.
    print(iter.next().value == "next");
  })();

// mjsunit/regress/regress-857.js
print(1283326536000 == Date.parse("2010-08-31T22:35:36-09:00"));
print(1283261736000 == Date.parse("2010-08-31T22:35:36+09:00"));
print(1283326536000 == Date.parse("2010-08-31T22:35:36.0-09:00"));
print(1283261736000 == Date.parse("2010-08-31T22:35:36.0+09:00"));
// colon-less time expressions in time zone offsets are not conformant
// with ES5 15.9.1.15 but are nonetheless supported in V8
print(1283326536000 == Date.parse("2010-08-31T22:35:36-0900"));
print(1283261736000 == Date.parse("2010-08-31T22:35:36+0900"));

//mjsunit/compiler/regress-5538.js
(function() {
    function foo(x) {
      x = x | 0;
      return Number.parseInt(x + 1);
    }
  
    ArkTools.prepareFunctionForOptimization(foo);
    print(1 == foo(0));
    print(2 == foo(1));
    ArkTools.optimizeFunctionOnNextCall(foo);
    print(Math.pow(2, 31) == foo(Math.pow(2, 31) - 1));
  })();
  
  (function() {
    function foo(x) {
      x = x | 0;
      return Number.parseInt(x + 1, 0);
    }
  
    ArkTools.prepareFunctionForOptimization(foo);
    print(1 == foo(0));
    print(2 == foo(1));
    ArkTools.optimizeFunctionOnNextCall(foo);
    print(Math.pow(2, 31) == foo(Math.pow(2, 31) - 1));
  })();
  
  (function() {
    function foo(x) {
      x = x | 0;
      return Number.parseInt(x + 1, 10);
    }
  
    ArkTools.prepareFunctionForOptimization(foo);
    print(1 == foo(0));
    print(2 == foo(1));
    ArkTools.optimizeFunctionOnNextCall(foo);
    print(Math.pow(2, 31) == foo(Math.pow(2, 31) - 1));
  })();
  
  (function() {
    function foo(x) {
      x = x | 0;
      return Number.parseInt(x + 1, undefined);
    }
    ArkTools.prepareFunctionForOptimization(foo);
    print(1 == foo(0));
    print(2 == foo(1));
    ArkTools.optimizeFunctionOnNextCall(foo);
    print(Math.pow(2, 31) == foo(Math.pow(2, 31) - 1));
  })();

  // mjsunit/regress/regress-12256.js
  const datesList = [{ year: '2021', month: '10', day: '22', hour: '10', minute: '12', second: '32' },
  { year: '2021', month: '8', day: '3', hour: '9', minute: '9', second: '6' }];
  const { year, month, day, hour, minute, second } = datesList[0];
  const s0 = `${year}-${month}-${day} ${hour}:${minute}:${second}Z`;
  for (let i = 1; i < 10; i++) {
    const s1 = `${'0'.repeat(i) + year}-${month}-${day} ${hour}:${minute}:${second}Z`;
    print(new Date(s0).getTime() ==  new Date(s1).getTime());
  }

  // mjsunit/regress/regress-crbug-1262007.js
  function foo(...args) {
    class C {}
    C(...args);
  }
  Object.getPrototypeOf([])[Symbol.iterator] = () => {};
  ArkTools.prepareFunctionForOptimization(foo);
  try {
    foo()
  } catch(e) {
    print(e instanceof TypeError)
  }
