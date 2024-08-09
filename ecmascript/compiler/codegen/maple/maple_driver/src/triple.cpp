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

#include "triple.h"
#include "driver_options.h"

namespace maple {
void Triple::Init(bool isAArch64)
{
    /* Currently Triple is used only to configure aarch64: be/le, ILP32/LP64
     * Other architectures (TARGX86_64, TARGX86, TARGARM32, TARGVM) are configured with compiler build config */
    if (isAArch64) {
        arch = Triple::ArchType::aarch64;
        environment = Triple::EnvironmentType::GNU;
    } else {
        arch = Triple::ArchType::x64;
        environment = Triple::EnvironmentType::GNU;
    }
}
}  // namespace maple
