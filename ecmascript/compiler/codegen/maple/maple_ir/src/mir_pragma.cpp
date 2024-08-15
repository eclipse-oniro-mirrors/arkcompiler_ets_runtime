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

#include "mir_pragma.h"
#include "mir_nodes.h"
#include "mir_function.h"
#include "printing.h"
#include "maple_string.h"
#include <iomanip>

namespace {
enum Status {
    kStop = 0,
    kStartWithSubvec = 1,
    kNormalTypeStrEndWithSemicolon = 2,
    kNormalTypeStrEndWithSubvecNeedsSemicolon = 3,
    kEndWithSubvec = 4,
    kIgnoreAndContinue = 5
};
}