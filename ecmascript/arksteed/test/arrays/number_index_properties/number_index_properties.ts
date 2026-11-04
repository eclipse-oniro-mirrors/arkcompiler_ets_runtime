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

// @ts-nocheck

//! PARAMS --compiler-jit-hotness-threshold=60000

//! METHOD fractionIndexRead
//! HAS LoadTaggedElement
//! HAS F64ToI32Trunc
function fractionIndexRead(array, key) { return array[key]; }

//! METHOD nanIndexRead
//! HAS LoadTaggedElement
//! HAS F64ToI32Trunc
function nanIndexRead(array, key) { return array[key]; }

//! METHOD infiniteIndexRead
//! HAS LoadTaggedElement
//! HAS F64ToI32Trunc
function infiniteIndexRead(array, key) { return array[key]; }

//! METHOD negativeIndexRead
//! HAS LoadTaggedElement
//! HAS F64ToI32Trunc
function negativeIndexRead(array, key) { return array[key]; }

//! METHOD largeIndexRead
//! HAS LoadTaggedElement
//! HAS F64ToI32Trunc
function largeIndexRead(array, key) { return array[key]; }

//! METHOD stringIndexRead
//! HAS LoadTaggedElement
//! HAS F64ToI32Trunc
function stringIndexRead(array, key) { return array[key]; }

const values = ['zero', 'one', 'two'];
values['1.5'] = 'fraction';
values['NaN'] = 'nan';
values['Infinity'] = 'infinite';
values['-1'] = 'negative';
values['4294967295'] = 'large';
values['01'] = 'leading';
for (let i = 0; i < 64; i++) fractionIndexRead(values, 1);
ArkTools.arkSteedCompileSync(fractionIndexRead);
for (let i = 0; i < 64; i++) nanIndexRead(values, 1);
ArkTools.arkSteedCompileSync(nanIndexRead);
for (let i = 0; i < 64; i++) infiniteIndexRead(values, 1);
ArkTools.arkSteedCompileSync(infiniteIndexRead);
for (let i = 0; i < 64; i++) negativeIndexRead(values, 1);
ArkTools.arkSteedCompileSync(negativeIndexRead);
for (let i = 0; i < 64; i++) largeIndexRead(values, 1);
ArkTools.arkSteedCompileSync(largeIndexRead);
for (let i = 0; i < 64; i++) stringIndexRead(values, 1);
ArkTools.arkSteedCompileSync(stringIndexRead);
print(ArkTools.arkSteedIsCompiled(fractionIndexRead));
print(fractionIndexRead(values, 1.5));
print(ArkTools.arkSteedIsCompiled(nanIndexRead));
print(nanIndexRead(values, NaN));
print(ArkTools.arkSteedIsCompiled(infiniteIndexRead));
print(infiniteIndexRead(values, Infinity));
print(ArkTools.arkSteedIsCompiled(negativeIndexRead));
print(negativeIndexRead(values, -1));
print(ArkTools.arkSteedIsCompiled(largeIndexRead));
print(largeIndexRead(values, 4294967295));
print(ArkTools.arkSteedIsCompiled(stringIndexRead));
print(stringIndexRead(values, '01'));
print(values.length);
print('OK');
