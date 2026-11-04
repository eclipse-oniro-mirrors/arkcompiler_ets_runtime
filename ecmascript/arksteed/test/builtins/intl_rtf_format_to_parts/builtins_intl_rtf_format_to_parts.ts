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

// Migrated from test262 (reduced):
//   intl402/RelativeTimeFormat/prototype/formatToParts/pl-pl-style-short.js
// (esid: sec-Intl.RelativeTimeFormat.prototype.format)
//
// Checks Intl.RelativeTimeFormat.prototype.formatToParts in pl-PL short
// style: part types, numeric grouping and plural unit selection. The
// migration keeps three units and a representative value set; the oracle
// strings are the joined part values.

//! METHOD builtins_intl_rtf_format_to_parts
//! COMPILE_COUNT_GE *builtins_intl_rtf_format_to_parts* 1
//! EXEC_COUNT_GE *builtins_intl_rtf_format_to_parts* 1

function builtins_intl_rtf_format_to_parts(): void
{
    let rtf = new Intl.RelativeTimeFormat("pl-PL", { "style": "short" });
    print(typeof rtf.formatToParts === "function" ? 1 : 0);

    function partsLine(value: number, unit: string): string
    {
        let parts = rtf.formatToParts(value, unit);
        let line = "";
        for (let i = 0; i < parts.length; i++) {
            line += parts[i].value;
        }
        return line;
    }

    print(partsLine(1000, "second"));
    print(partsLine(10, "minute"));
    print(partsLine(2, "day"));
    print(partsLine(1, "day"));
    print(partsLine(0, "day"));
    print(partsLine(-1, "day"));
    print(partsLine(-10, "day"));
    print(partsLine(1000, "year"));
    print(partsLine(2, "year"));
    print(partsLine(1, "year"));
    print(partsLine(123456.78, "second"));
}

builtins_intl_rtf_format_to_parts();

ArkTools.arkSteedCompileSync(builtins_intl_rtf_format_to_parts);

builtins_intl_rtf_format_to_parts();
