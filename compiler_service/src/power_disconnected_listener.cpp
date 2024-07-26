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

#include "power_disconnected_listener.h"
#include "aot_compiler_impl.h"
#include "ecmascript/log_wrapper.h"

namespace OHOS::ArkCompiler {
PowerDisconnectedListener::PowerDisconnectedListener(const EventFwk::CommonEventSubscribeInfo &subscribeInfo)
    : EventFwk::CommonEventSubscriber(subscribeInfo)
{
}

void PowerDisconnectedListener::OnReceiveEvent(const EventFwk::CommonEventData &data)
{
    LOG_SA(INFO) << "PowerDisconnectedListener::OnReceiveEvent";
    AotCompilerImpl::GetInstance().HandlePowerDisconnected();
}
}  // namespace OHOS::ArkCompiler