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
class Car {
    make: string;
    model: string;
    year: number;

    constructor(make: string = "Unknown", model: string = "Model", year: number = 2020) {
        this.make = make;
        this.model = model;
        this.year = year;
    }

    getDescription(): string {
        return `${this.year} ${this.make} ${this.model}`;
    }
}

// G11.6 additive evidence (arksteed-g116-call-closure-and-additive-evidence#C1,#C3):
// the original target `Car` keeps its own compilation and entry evidence, and
// the silent same-domain caller `probeDefaultConstruction` carries the actual
// construction form of this leaf. Program output is unchanged.
//! COMPILE_COUNT_GE Car 1
//! EXEC_COUNT_GE *Car 1
//! COMPILE_COUNT_GE probeDefaultConstruction 1
//! EXEC_COUNT_GE *probeDefaultConstruction 1
//! DEOPT_COUNT *Deoptimize* 0
//! METHOD probeDefaultConstruction
//! HAS CallRuntime OptNewObjRange

ArkTools.arkSteedCompileSync(Car);
ArkTools.waitJitCompileFinish(Car);


function probeDefaultConstruction() {
    return [new Car(), new Car("Toyota"), new Car("Honda", "Civic"), new Car("Ford", "Mustang", 2023)];
}
ArkTools.arkSteedCompileSync(probeDefaultConstruction);
ArkTools.waitJitCompileFinish(probeDefaultConstruction);


let car1 = new Car();
print(car1.make);
print(car1.model);
print(car1.year);
print(car1.getDescription());

let car2 = new Car("Toyota");
print(car2.make);
print(car2.model);
print(car2.year);
print(car2.getDescription());

let car3 = new Car("Honda", "Civic");
print(car3.make);
print(car3.model);
print(car3.year);
print(car3.getDescription());

let car4 = new Car("Ford", "Mustang", 2023);
print(car4.make);
print(car4.model);
print(car4.year);
print(car4.getDescription());

// Bounded silent preparation: the same four construction forms through the
// compiled caller. It prints nothing and changes no earlier observation.
probeDefaultConstruction();
