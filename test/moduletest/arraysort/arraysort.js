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

class DeepProxy {
    constructor(obj, handler) {
        return new Proxy(obj, handler);
    }
}
class ClassB {
    constructor(n) {
        this.n = 0;
        this.n = n;
    }
}

let nextFreeId = 0;
class ClassA {
    constructor(a, b) {
        this.a = a;
        this.b = new ClassB(b);
        this.id = nextFreeId++;
    }
}

// Testing the proxy situation.
let data1 = [new ClassA(1, 10), new ClassA(3, 30), new ClassA(4, 40), new ClassA(2, 20), new ClassA(11, 250)];
let objHandler1 = new DeepProxy(data1, {});
print(JSON.stringify(objHandler1));
objHandler1.sort((a, b) => {
    return a.b.n - b.b.n;
})
print(JSON.stringify(objHandler1));

// Testing cases with both proxy and hole.
let data2 = [new ClassA(1, 10), , new ClassA(3, 30), , new ClassA(4, 40), new ClassA(2, 20), new ClassA(11, 250)];
let objHandler2 = new DeepProxy(data2, {
    deleteProperty(target, prop) {
        print(`delete ${prop.toString()}`);
        return Reflect.deleteProperty(target, prop);
    }
});
objHandler2.sort((a, b) => {
    return a.b.n - b.b.n;
})
print(JSON.stringify(objHandler2));

/*
 * Test Case Description:
 * 1. This use case is used to verify the logical processing order of the binary insertion sorting algorithm.
 * 2. If there are any changes to the use case, please confirm if the use case needs to be modified.
 */
let arr1 = [1, 3, 2];
arr1.sort((a, b) => {
    return a - b;
});
print(JSON.stringify(arr1));

/*
 * Test Case Description:
 * 1. This use case is used to verify the logical processing order of the quick sorting algorithm.
 * 2. If there are any changes to the use case, please confirm if the use case needs to be modified.
 */
for (let i = 0; i < 100; i++) {
    arr1[i] = i;
}
arr1[0] = 99;
arr1[99] = 0;
arr1[49] = 50;
arr1[50] = 49;
arr1.sort((a, b) => {
    return a - b;
})
print(JSON.stringify(arr1));

// Modification of objects during the comparison process.
let arr2 = [1, 3, 2];
arr2.sort((a, b) => {
    if (a == 1 || b == 1) {
        arr2[0] == 2;
    }
    return a - b;
});
print(JSON.stringify(arr2));

let arr3 = [1, 3, 2];
arr3.sort((a, b) => {
    if (a == 1 || b == 1) {
        arr3[4] == 2;
    }
    return a - b;
});
print(JSON.stringify(arr3));

// Testing the situation where this is an Object
let obj1 = {0: 1, 1: 3, a: 6, 2: 2, length: 3};
Array.prototype.sort.call(obj1, (a, b) => {
    return a - b;
});
print(JSON.stringify(obj1));

let obj2 = {0: 1, 1: 3, a: 6, 2: 2, length: 3};
Array.prototype.sort.call(obj2, (a, b) => {
    if (a == 1 || b == 1) {
        obj2.a = 60;
    }
    return a - b;
});
print(obj2.a == 60);
print(JSON.stringify(obj2));

let obj3 = {0: 1, 1: 3, a: 6, 2: 2, length: 2};
Array.prototype.sort.call(obj3, (a, b) => {
    return a - b;
});
print(obj3[1] == 3)
print(JSON.stringify(obj3));

let obj4 = {0: 1, 1: 3, a: 6, 3: 2, length: 4};
Array.prototype.sort.call(obj4, (a, b) => {
    return a - b;
});
print(obj4[2] == 3)
print(JSON.stringify(obj4));

// Test if this is a Map type;
let map1 = new Map();
map1.set(0, 1);
map1.set(1, 3);
map1.set(2, 2);
map1.set("a", 6);
map1.set("length", 3);
Array.prototype.sort.call(map1, (a, b) => {
    return a - b;
});
print(JSON.stringify(map1));

let map2 = new Map();
map2.set(0, 1);
map2.set(1, 3);
map2.set(2, 2);
map2.set("a", 6);
map2.set("length", 3);
Array.prototype.sort.call(map2, (a, b) => {
    if (a == 1 || b == 1) {
        map2.set("a", 60);
    }
    return a - b;
});
print(JSON.stringify(map2));

// Test prototype
let child1 = [1, 3, 2];
let proto1 = [4, 7, 5];
child1.__proto__ = proto1;
child1.sort((a, b) => {
    return a - b;
});
print(JSON.stringify(child1));

let child2 = [1, , 2];
child2.__proto__ = proto1;
child2.sort((a, b) => {
    return a - b;
});
print(child2.hasOwnProperty('1'));
print(JSON.stringify(child2));

let child3 = [1, 3, 2];
let proto2 = [4, , 5];
child3.__proto__ = proto2;
child3.sort((a, b) => {
    return a - b;
});
print(JSON.stringify(child3));

let child4 = [1, , 2];
child4.__proto__ = proto2;
child4.sort((a, b) => {
    return a - b;
});
print(child4.hasOwnProperty('2'));
print(JSON.stringify(child4));

var test1 = [-321, 65, 0, -3215, 653, 650, -3210, -2147483648, 2147483647];
print(test1.sort());


var arr4 = new Array(5);
arr4[0] = 93;
arr4[1] = 930;
arr4[2] = -45;
arr4[3] = 44;
arr4[4] = 0;
print(arr4.sort(function(a, b){
    a--;
    b--;
    return b-a;
}));

var arr5 = [3, 1, 4];
arr5.sort((a, b) => {
    if (a == 1 || b == 1) {
        arr5[0] = 6;
    }
    return a - b;
});
print(arr5);

Object.defineProperty(Array.prototype, "tt", {
    value:37,
    writable:false,
});

var arr6 = new Array(5);
arr6[0] = 93;
arr6[2] = -45;
arr6[3] = "djs";
arr6[4] = 0;
print(arr6.sort());

var arr7 = [1];
print(arr7.sort());

var res1 = Array.prototype.sort.call("m", Uint8Array);
print(res1);

try {
    Array.prototype.sort.call("mm", Uint8Array);
} catch (e) {
    print(e.name);
}

const o1 = {
    ..."654"
};
const arr8 = [1, 2, 3];
const o2 = {
    __proto__: arr8,
    ...o1
};
o2.sort();
print(o2[0]);
print(o2[1]);
print(o2[2]);

const bigint64_array = new BigInt64Array();
const proxy = new Proxy([1, 2, 3], bigint64_array);
try {
    proxy.sort();
} catch (e) {
    print(e.name);
}

try {
    const vSort = new Float64Array(Float64Array);
    vSort.__proto__.sort();
} catch (e) {
    print(e.message);
}

const v4 = [1, 255];
class C5 extends Int16Array{
    toString(a7, a8, a9, a10) {
        super.includes();
    }
}
try {
    const v12 = new C5();
    v4.sort(C5.prototype.toString);
} catch (e) {
    print(e.message);
}
const items = [
    { name: "Edward", value: 21 },
    { name: "Sharpe", value: 37 },
    { name: "And", value: 45 },
    { name: "The", value: -12 },
    { name: "Magnetic", value: 13 },
    { name: "Zeros", value: 37 },
  ];

items.sort((a, b) => a.value - b.value);
print(JSON.stringify(items));

items.sort((a, b) => {
    const nameA = a.name.toUpperCase();
    const nameB = b.name.toUpperCase();
    if (nameA < nameB) {
      return -1;
    }
    if (nameA > nameB) {
      return 1;
    }
    return 0;
  });

print(JSON.stringify(items));

const numbers = [3, 1, 4, 1, 5];
const sorted = numbers.sort((a, b) => a - b);
sorted[0] = 10;
print(numbers[0]); // 10

const students = [
    { name: "Alex", grade: 15 },
    { name: "Devlin", grade: 15 },
    { name: "Eagle", grade: 13 },
    { name: "Sam", grade: 14 },
];
// stable
students.sort((firstItem, secondItem) => firstItem.grade - secondItem.grade);
print(JSON.stringify(students));
const v2 =[];
class C3{};
v2.__proto__ = C3;
let arr = new Array(518);
for(let i=0;i<518;i++){
    arr[i]=""+i;
}
arr[512]="valueOf";
arr[513]="p0";
arr[514]="p1";
arr[515]="p2";
arr[516]="p3";
arr[517]="p4";
arr.sort();

//for cmp return type is double
let arr34 = [2.63, 1.67];
print(arr34.sort((a, b)=> a - b));
print("sort Test Success!")

{
    print("---------------test for stable array sort where arr changeto dictionary array during sorting-----------")
    let arr = new Array(20);
    let haschang = false;
    for (let i = 0; i < arr.length; i++) {
        arr[i] = i;
    }
    arr[1] = {
        toString() {
            if (!haschang) {
                arr.length = 102400;
            }
            return -999;
        }
    }
    arr.sort();
    arr.splice(-1, 1);
    print("---------------test for stable array sort where arr changeto dictionary array during sorting end-------")
}
{
    print("---------------test for stable array sort where elements length change---------------------------------")
    let arr = new Array(20);
    let haschang = false;
    for (let i = 0; i < arr.length; i++) {
        arr[i] = i;
    }
    arr[1] = {
        toString() {
            if (!haschang) {
                arr.splice(-1, 1);
            }
            return -999;
        }
    }
    arr.sort();
    arr.splice(-1, 1)
    print("---------------test for stable array sort where elements length change end-----------------------------")
}
