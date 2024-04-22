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

#include "ecmascript/module/js_dynamic_import.h"
#include "ecmascript/base/path_helper.h"
#include "ecmascript/builtins/builtins_promise_job.h"
#include "ecmascript/js_function.h"
#include "ecmascript/module/js_module_deregister.h"
#include "ecmascript/module/js_module_manager.h"
#include "ecmascript/module/module_data_extractor.h"

namespace panda::ecmascript {
using PathHelper = base::PathHelper;
using BuiltinsPromiseJob = builtins::BuiltinsPromiseJob;

JSTaggedValue DynamicImport::ExecuteNativeOrJsonModule(JSThread *thread, JSHandle<EcmaString> specifierString,
                                                       ModuleTypes moduleType,
                                                       JSHandle<JSPromiseReactionsFunction> resolve,
                                                       JSHandle<JSPromiseReactionsFunction> reject,
                                                       const JSPandaFile *jsPandaFile)
{
    ModuleManager *moduleManager = thread->GetCurrentEcmaContext()->GetModuleManager();
    JSMutableHandle<JSTaggedValue> requiredModule(thread, thread->GlobalConstants()->GetUndefined());
    if (moduleManager->IsImportedModuleLoaded(specifierString.GetTaggedValue())) {
        ModuleDeregister::ReviseLoadedModuleCount(thread, specifierString.GetTaggedValue());
        JSHandle<SourceTextModule> moduleRecord =
            moduleManager->HostGetImportedModule(specifierString.GetTaggedValue());
        requiredModule.Update(moduleRecord);
    } else {
        CString requestPath = ConvertToString(specifierString.GetTaggedValue());
        JSHandle<SourceTextModule> moduleRecord(thread, thread->GlobalConstants()->GetUndefined());
        if (moduleType != ModuleTypes::JSON_MODULE) {
            // nativeModule
            JSHandle<JSTaggedValue> nativeModuleHld = moduleManager->ResolveNativeModule(requestPath, "", moduleType);
            moduleRecord = JSHandle<SourceTextModule>::Cast(nativeModuleHld);
            if (!SourceTextModule::LoadNativeModule(thread, moduleRecord, moduleType)) {
                LOG_FULL(ERROR) << " dynamically loading native module" << requestPath << " failed";
            }
        } else {
            // json
            moduleRecord = JSHandle<SourceTextModule>::Cast(ModuleDataExtractor::ParseJsonModule(
                thread, jsPandaFile, jsPandaFile->GetJSPandaFileDesc(), requestPath));
        }
        moduleRecord->SetStatus(ModuleStatus::EVALUATED);
        moduleRecord->SetLoadingTypes(LoadingTypes::DYNAMITC_MODULE);
        moduleRecord->SetRegisterCounts(1);
        thread->GetEcmaVM()->PushToDeregisterModuleList(requestPath);
        requiredModule.Update(moduleRecord);
    }

    JSHandle<JSTaggedValue> moduleNamespace = SourceTextModule::GetModuleNamespace(thread,
        JSHandle<SourceTextModule>(requiredModule));
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, BuiltinsPromiseJob::CatchException(thread, reject));
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    EcmaRuntimeCallInfo *info =
        EcmaInterpreter::NewRuntimeCallInfo(thread,
                                            JSHandle<JSTaggedValue>(resolve),
                                            undefined, undefined, 1);
    RETURN_VALUE_IF_ABRUPT_COMPLETION(thread, BuiltinsPromiseJob::CatchException(thread, reject));
    info->SetCallArg(moduleNamespace.GetTaggedValue());
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    return JSFunction::Call(info);
}
}  // namespace panda::ecmascript
