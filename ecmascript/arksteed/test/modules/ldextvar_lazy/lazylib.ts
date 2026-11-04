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

// Sibling module of ldextvar_lazy: exports both a lazily and an ordinarily
// imported binding. bump() mutates `v` so the test can observe live-binding
// semantics through the lazy read after evaluation.

export let v = 42;

export let w = 43;

export function bump(): number {
    return ++v;
}
