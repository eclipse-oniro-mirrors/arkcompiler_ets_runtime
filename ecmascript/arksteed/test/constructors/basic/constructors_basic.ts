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
class Person {
    name: string;
    age: number;

    constructor(name: string, age: number) {
        this.name = name;
        this.age = age;
    }

    greet(): string {
        return `Hello, my name is ${this.name} and I'm ${this.age} years old.`;
    }
}

// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3):
// the original target `Person` keeps its own compilation and entry evidence, and
// the silent same-domain caller `probeBasicConstruction` carries the actual
// construction form of this leaf in its own dump. Program output is unchanged.
//! COMPILE_COUNT_GE Person 1
//! EXEC_COUNT_GE *Person 1
//! COMPILE_COUNT_GE probeBasicConstruction 1
//! EXEC_COUNT_GE *probeBasicConstruction 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD probeBasicConstruction
//! HAS CallRuntime OptNewObjRange

ArkTools.arkSteedCompileSync(Person);
ArkTools.waitJitCompileFinish(Person);

function probeBasicConstruction(name: string, age: number) {
    return new Person(name, age);
}
ArkTools.arkSteedCompileSync(probeBasicConstruction);
ArkTools.waitJitCompileFinish(probeBasicConstruction);


let person = new Person("Alice", 30);
print(person.name);
print(person.age);
print(person.greet());

// Bounded silent preparation: one more construction through the compiled
// same-domain caller. It prints nothing and changes no earlier observation.
probeBasicConstruction("Bob", 40);