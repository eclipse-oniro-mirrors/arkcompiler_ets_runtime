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
try {
    new Intl.DateTimeFormat("en" , { timeZone: "US/Alaska0" });
} catch (e) {
    print(e instanceof RangeError);
}

// This case aims to check stack overflow while timeZone is a long string
try {
    new Intl.DateTimeFormat("en", {timeZone: Array(0x8000).join("a")});
} catch (e) {
    print(e);
}
