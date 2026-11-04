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
 * @tc.name:sendableset
 * @tc.desc:test sendableset
 * @tc.type: FUNC
 * @tc.require: issue#I93TZC
 */

// @ts-nocheck
declare function print(str: any): string;

function FillSet(set: SendableSet): void {
  for (let i = 0; i < 5; i++) {
    set.add(i);
  }
}
let sharedSet: SendableSet = new SendableSet<number>();

// Basic tests
print("===Basic test begin===")
FillSet(sharedSet);
print("set size is " + sharedSet.size);
print(SendableSet[Symbol.species] == SendableSet);
print(SendableSet.name == 'SendableSet');
print(SendableSet[Symbol.species] == Set);

const keyIter = sharedSet.keys();
let nextEntry = keyIter.next();
print("keys next:" + nextEntry.value + ", done: " + nextEntry.done);
nextEntry = keyIter.next();
print("keys next:" + nextEntry.value + ", done: " + nextEntry.done);
nextEntry = keyIter.next();
print("keys next:" + nextEntry.value + ", done: " + nextEntry.done);
nextEntry = keyIter.next();
print("keys next:" + nextEntry.value + ", done: " + nextEntry.done);
nextEntry = keyIter.next();
print("keys next:" + nextEntry.value + ", done: " + nextEntry.done);
nextEntry = keyIter.next();
print("keys next:" + nextEntry.value + ", done: " + nextEntry.done);

const valueIter = sharedSet.keys();
nextEntry = valueIter.next();
print("values next:" + nextEntry.value + ", done: " + nextEntry.done);
nextEntry = valueIter.next();
print("values next:" + nextEntry.value + ", done: " + nextEntry.done);
nextEntry = valueIter.next();
print("values next:" + nextEntry.value + ", done: " + nextEntry.done);
nextEntry = valueIter.next();
print("values next:" + nextEntry.value + ", done: " + nextEntry.done);
nextEntry = valueIter.next();
print("values next:" + nextEntry.value + ", done: " + nextEntry.done);
nextEntry = valueIter.next();
print("values next:" + nextEntry.value + ", done: " + nextEntry.done);

sharedSet.forEach((key: number, value: number, set: SendableSet) => {
  print('set key[forEach]:' + 'key:' + key + ', value:' + value);
});

print(sharedSet[Symbol.toStringTag] == 'SendableSet');
for (let iter of sharedSet[Symbol.iterator]()) {
  print("set key[Symbol.iterator]:" + iter);
}
print(sharedSet[Symbol.iterator] == sharedSet.values);
print(sharedSet[Symbol.iterator] == sharedSet.keys);

print(sharedSet.has(4));
sharedSet.add(4);
print(sharedSet.size == 5);
print(sharedSet.has(10));
sharedSet.add(10);
print(sharedSet.size == 6);
print(sharedSet.has(10));
sharedSet.delete(10);
print(sharedSet.has(10));
print(sharedSet.size == 5);
sharedSet.clear();
print(sharedSet.size == 0);
try {
  sharedSet["extension"] = "value";
} catch(e) {
  print("add extension(.): " + e);
}
try {
  sharedSet.extension = "value";
} catch(e) {
  print("add extension([]): " + e);
}

print("===Basic test end===");

// No Expected Concurrent modification exception while iterating using iterators
print("===Concurrent modification during iteration Test(iterator) begin===")
sharedSet.clear();
FillSet(sharedSet);
print("set size is " + sharedSet.size);

const iterator = sharedSet.entries();
for (const [key, _] of iterator) {
  print("set key[for-of]: " + key);
}
try {
  const iterator = sharedSet.entries();
  for (const [key, _] of iterator) {
    if (key == 1) {
      sharedSet.add(key + 5);
    }
  }
  print("Add Scenario[for-of] updated size: " + sharedSet.size);
} catch (e) {
  print("Add Scenario[for-of]: " + e);
}
try {
  const iterator = sharedSet.entries();
  for (const [key, _] of iterator) {
    if (key % 2 == 0) {
      sharedSet.delete(key);
    }
  }
  print("Delete Scenario[for-of] updated size: " + sharedSet.size);
} catch (e) {
  print("Delete Scenario[for-of]: " + e);
}
try {
  const iterator = sharedSet.entries();
  for (const [key, _] of iterator) {
    sharedSet.clear();
  }
  print("Clear Scenario[for-of] updated size: " + sharedSet.size);
} catch (e) {
  print("Clear Scenario[for-of]: " + e);
}

sharedSet.clear();
FillSet(sharedSet);
print("set size is " + sharedSet.size);
try {
  const iterator = sharedSet.entries();
  sharedSet.add(6);
  iterator.next();
  print("Add Scenario[next()] updated size: " + sharedSet.size);
} catch (e) {
  print("Add Scenario[next()]: " + e);
}
try {
  const iterator = sharedSet.entries();
  sharedSet.delete(6);
  iterator.next();
  print("Delete Scenario[next()] updated size: " + sharedSet.size);
} catch (e) {
  print("Delete Scenario[next()]: " + e);
}
try {
  const iterator = sharedSet.entries();
  sharedSet.clear();
  iterator.next();
  print("Clear Scenario[next()] updated size: " + sharedSet.size);
} catch (e) {
  print("Clear Scenario[next()]: " + e);
}
print("===Concurrent modification during iteration Test(iterator) end===")

// Expected Concurrent modification exception while iterating using forEach
print("===Concurrent modification during iteration Test(forEach) begin===")
sharedSet.clear();
FillSet(sharedSet);
print("set size is " + sharedSet.size);
sharedSet.forEach((key: number, _: number, set: SendableSet) => {
  print('set key[forEach]: ' + key);
});
try {
  sharedSet.forEach((key: number, _: number, set: SendableSet) => {
    set.add(key + 5);
  });
} catch (e) {
  print("Add Scenario[forEach]: " + e + ", errCode: " + e.code);
}
try {
  sharedSet.forEach((key: number, _: number, set: SendableSet) => {
    if (key % 2 == 0) {
      set.delete(key);
    }
  });
} catch (e) {
  print("Delete Scenario[forEach]: " + e + ", errCode: " + e.code);
}
try {
  sharedSet.forEach((key: number, _: number, set: SendableSet) => {
    set.clear();
  });
} catch (e) {
  print("Clear Scenario[forEach]: " + e + ", errCode: " + e.code);
}
print("===Concurrent modification during iteration Test(forEach) end===");

print("===Type check begin===");
class SObject {
  constructor() {
    "use sendable"
  }
};

try {
  let sObj = new SObject();
  sharedSet = new SendableSet(['str', 1, sObj, undefined, true, null]);
  print("sharedSet add[shared] element success");
} catch (e) {
  print("sharedSet add[unshared]: " + e + ", errCode: " + e.code);
}

try {
  let obj = {}
  sharedSet = new SendableSet([obj]);
} catch (e) {
  print("sharedSet add[unshared]: " + e + ", errCode: " + e.code);
}

try {
  let sym = Symbol("testSymbol")
  sharedSet = new SendableSet([sym, 2]);
} catch (e) {
  print("sharedSet add[unshared]: " + e + ", errCode: " + e.code);
}

print("=== An iterable object to convert to an ArkTS Set  begin===")
sharedSet.clear();
FillSet(sharedSet);
try {
  
  const iterator = sharedSet.entries();
  let sharedSet1: SendableSet = new SendableSet<>(iterator);
  sharedSet1.forEach((key: number, _: number, set: SendableSet) => {
    print("set key[forEach]: " + key);
  })
} catch (e) {
  print("SendableSetConstructor Scenario[next()]: " + e);
}

print("===Type check end===");

// IR-ization UT supplement for the add/delete/has/forEach/values/clear/size
// stubs: fresh/empty boundaries, duplicate add, missing delete, values
// iteration protocol, @@iterator alias, and callback exception propagation
// with scope release.
function setEdgeCases(): void {
    print("===set edge cases begin===");

    // Fresh set: size 0, has -> false, delete missing -> false.
    const fresh = new SendableSet<number>();
    print("fresh set size: " + fresh.size);
    print("fresh has: " + fresh.has(1));
    print("fresh delete: " + fresh.delete(1));

    // Duplicate add: size unchanged.
    const dup = new SendableSet<number>();
    dup.add(1);
    dup.add(1);
    print("dup add size: " + dup.size);

    // values: for-of iteration, next() protocol, @@iterator alias.
    const vals = new SendableSet<number>();
    vals.add(10);
    vals.add(20);
    vals.add(30);
    let acc = "";
    for (const v of vals.values()) {
        acc += v + ";";
    }
    print("values for-of: " + acc);
    const valsIter = vals.values();
    const n1 = valsIter.next();
    const n2 = valsIter.next();
    print("values next: " + n1.value + "," + n2.value);
    const n3 = valsIter.next();
    print("values last: " + n3.value + "," + n3.done);
    print("values done: " + valsIter.next().done);
    print("iterator alias: " + (vals[Symbol.iterator] === vals.values));

    // values on an empty set: done immediately.
    const emptyIter = fresh.values();
    print("empty values done: " + emptyIter.next().done);

    // forEach: callback exception propagates; set remains usable (scope released).
    try {
        vals.forEach((v: number) => {
            if (v == 20) {
                throw new Error("set forEach cb throw");
            }
        });
    } catch (err) {
        print("set forEach cb exception: " + err);
    }
    print("set after exception size: " + vals.size);

    // clear on an empty set: no error.
    fresh.clear();
    print("clear empty size: " + fresh.size);

    print("===set edge cases end===");
}
setEdgeCases();

// IR-ization UT supplement: subclass receiver for the
// add/delete/has/forEach/values/clear/size stubs, and values iteration
// order after delete + re-add (insertion order).
class SubSendableSet<T> extends SendableSet<T> {
    constructor() {
        'use sendable';
        super();
    }
}

function setSubclassEdgeCases(): void {
    print("===set subclass edge cases begin===");

    // values iteration order after delete + re-add: re-added entry goes to the end.
    const ord = new SendableSet<number>();
    ord.add(1);
    ord.add(2);
    ord.add(3);
    ord.delete(2);
    ord.add(2);
    let ordAcc = "";
    for (const v of ord.values()) {
        ordAcc += v + ";";
    }
    print("values order after re-add: " + ordAcc);

    // Subclass receiver: full round-trip over the in-scope methods.
    const sub = new SubSendableSet<number>();
    sub.add(1);
    sub.add(2);
    print("subset size: " + sub.size);
    print("subset has: " + sub.has(1));
    print("subset delete: " + sub.delete(2));
    let subAcc = "";
    for (const v of sub.values()) {
        subAcc += v + ";";
    }
    print("subset values: " + subAcc);
    sub.forEach((v: number) => {
        print("subset forEach: " + v);
    });
    sub.clear();
    print("subset cleared size: " + sub.size);

    print("===set subclass edge cases end===");
}
setSubclassEdgeCases();
