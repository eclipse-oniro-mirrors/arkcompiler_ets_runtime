/*
 * Copyright (c) 2021 Huawei Device Co., Ltd.
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

#include "ecmascript/builtins/builtins_promise_job.h"

#include "ecmascript/ecma_macros.h"
#include "ecmascript/global_env.h"
#include "ecmascript/interpreter/interpreter.h"
#include "ecmascript/js_function.h"
#include "ecmascript/js_handle.h"
#include "ecmascript/jspandafile/js_pandafile_executor.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/js_promise.h"
#include "ecmascript/js_tagged_value.h"
#include "ecmascript/module/js_module_manager.h"
#include "ecmascript/require/js_cjs_module.h"
#include "libpandabase/macros.h"

namespace panda::ecmascript::builtins {
JSTaggedValue BuiltinsPromiseJob::PromiseReactionJob(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    BUILTINS_API_TRACE(argv->GetThread(), PromiseJob, Reaction);
    JSThread *thread = argv->GetThread();
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    // 1. Assert: reaction is a PromiseReaction Record.
    JSHandle<JSTaggedValue> value = GetCallArg(argv, 0);
    ASSERT(value->IsPromiseReaction());
    JSHandle<PromiseReaction> reaction = JSHandle<PromiseReaction>::Cast(value);
    JSMutableHandle<JSTaggedValue> argument(GetCallArg(argv, 1));

    const GlobalEnvConstants *globalConst = thread->GlobalConstants();
    // 2. Let promiseCapability be reaction.[[Capabilities]].
    JSHandle<PromiseCapability> capability(thread, reaction->GetPromiseCapability());
    // 3. Let handler be reaction.[[Handler]].
    JSHandle<JSTaggedValue> handler(thread, reaction->GetHandler());
    JSMutableHandle<JSTaggedValue> call(thread, capability->GetResolve());
    const int32_t argsLength = 1;
    JSHandle<JSTaggedValue> undefined = globalConst->GetHandledUndefined();
    if (handler->IsString()) {
        // 4. If handler is "Identity", let handlerResult be NormalCompletion(argument).
        // 5. Else if handler is "Thrower", let handlerResult be Completion{[[type]]: throw, [[value]]: argument,
        // [[target]]: empty}.

        if (EcmaStringAccessor::StringsAreEqual(handler.GetObject<EcmaString>(),
            globalConst->GetHandledThrowerString().GetObject<EcmaString>())) {
            call.Update(capability->GetReject());
        }
    } else {
        // 6. Else, let handlerResult be Call(handler, undefined, «argument»).
        EcmaRuntimeCallInfo *info =
            EcmaInterpreter::NewRuntimeCallInfo(thread, handler, undefined, undefined, argsLength);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        info->SetCallArg(argument.GetTaggedValue());
        JSTaggedValue taggedValue = JSFunction::Call(info);
        argument.Update(taggedValue);
        // 7. If handlerResult is an abrupt completion, then
        // a. Let status be Call(promiseCapability.[[Reject]], undefined, «handlerResult.[[value]]»).
        // b. NextJob Completion(status).
        if (thread->HasPendingException()) {
            JSHandle<JSTaggedValue> throwValue = JSPromise::IfThrowGetThrowValue(thread);
            argument.Update(throwValue.GetTaggedValue());
            thread->ClearException();
            call.Update(capability->GetReject());
        }
    }
    // 8. Let status be Call(promiseCapability.[[Resolve]], undefined, «handlerResult.[[value]]»).
    EcmaRuntimeCallInfo *runtimeInfo =
        EcmaInterpreter::NewRuntimeCallInfo(thread, call, undefined, undefined, argsLength);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    runtimeInfo->SetCallArg(argument.GetTaggedValue());
    return JSFunction::Call(runtimeInfo);
}

JSTaggedValue BuiltinsPromiseJob::PromiseResolveThenableJob(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    BUILTINS_API_TRACE(argv->GetThread(), PromiseJob, ResolveThenableJob);
    JSThread *thread = argv->GetThread();
    [[maybe_unused]] EcmaHandleScope handleScope(thread);
    JSHandle<JSTaggedValue> promise = GetCallArg(argv, 0);
    ASSERT(promise->IsJSPromise());
    // 1. Let resolvingFunctions be CreateResolvingFunctions(promiseToResolve).
    JSHandle<ResolvingFunctionsRecord> resolvingFunctions =
        JSPromise::CreateResolvingFunctions(thread, JSHandle<JSPromise>::Cast(promise));
    JSHandle<JSTaggedValue> thenable = GetCallArg(argv, 1);
    JSHandle<JSTaggedValue> then = GetCallArg(argv, BuiltinsBase::ArgsPosition::THIRD);

    // 2. Let thenCallResult be Call(then, thenable, «resolvingFunctions.[[Resolve]], resolvingFunctions.[[Reject]]»).
    const int32_t argsLength = 2; // 2: «resolvingFunctions.[[Resolve]], resolvingFunctions.[[Reject]]»
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    EcmaRuntimeCallInfo *info = EcmaInterpreter::NewRuntimeCallInfo(thread, then, thenable, undefined, argsLength);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    info->SetCallArg(resolvingFunctions->GetResolveFunction(), resolvingFunctions->GetRejectFunction());
    JSTaggedValue result = JSFunction::Call(info);
    JSHandle<JSTaggedValue> thenResult(thread, result);
    // 3. If thenCallResult is an abrupt completion,
    // a. Let status be Call(resolvingFunctions.[[Reject]], undefined, «thenCallResult.[[value]]»).
    // b. NextJob Completion(status).
    if (thread->HasPendingException()) {
        thenResult = JSPromise::IfThrowGetThrowValue(thread);
        thread->ClearException();
        JSHandle<JSTaggedValue> reject(thread, resolvingFunctions->GetRejectFunction());
        EcmaRuntimeCallInfo *runtimeInfo =
            EcmaInterpreter::NewRuntimeCallInfo(thread, reject, undefined, undefined, 1);
        RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
        runtimeInfo->SetCallArg(thenResult.GetTaggedValue());
        return JSFunction::Call(runtimeInfo);
    }
    // 4. NextJob Completion(thenCallResult).
    return result;
}

JSTaggedValue BuiltinsPromiseJob::DynamicImportJob(EcmaRuntimeCallInfo *argv)
{
    ASSERT(argv);
    BUILTINS_API_TRACE(argv->GetThread(), PromiseJob, DynamicImportJob);
    JSThread *thread = argv->GetThread();
    EcmaVM *vm = thread->GetEcmaVM();
    [[maybe_unused]] EcmaHandleScope handleScope(thread);

    JSHandle<JSPromiseReactionsFunction> resolve(GetCallArg(argv, 0));
    JSHandle<JSPromiseReactionsFunction> reject(GetCallArg(argv, 1));   // 1 : first argument
    JSHandle<EcmaString> dirPath(GetCallArg(argv, 2));                  // 2 : second argument
    JSHandle<JSTaggedValue> specifier(GetCallArg(argv, 3));             // 3 : third argument
    JSHandle<JSTaggedValue> recordName(GetCallArg(argv, 4));  

    // Let specifierString be Completion(ToString(specifier))
    JSHandle<EcmaString> specifierString = JSTaggedValue::ToString(thread, specifier);
    if (thread->HasPendingException()) {
        return CatchException(thread, reject);
    }

    JSHandle<EcmaString> moduleName;
    CString entryPoint = JSPandaFile::ENTRY_MAIN_FUNCTION;
    CString baseFilename = ConvertToString(dirPath.GetTaggedValue());
    CString fileNameStr = "";
    if (recordName->IsUndefined()) {
        moduleName = CjsModule::ResolveFilenameFromNative(thread, dirPath.GetTaggedValue(),
                                                          specifierString.GetTaggedValue());
        fileNameStr = ConvertToString(moduleName.GetTaggedValue());
    } else {
        CString recordNameStr = ConvertToString(recordName.GetTaggedValue());
        CString requestModule = ConvertToString(specifier.GetTaggedValue());
         const JSPandaFile *jsPandaFile =
            JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, baseFilename, recordNameStr.c_str());
        entryPoint =
            ModuleManager::ConcatFileNameWithMerge(jsPandaFile, baseFilename, recordNameStr, requestModule);
        fileNameStr = baseFilename;
        moduleName = thread->GetEcmaVM()->GetFactory()->NewFromUtf8(entryPoint);
    }

    const JSPandaFile *jsPandaFile = JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, fileNameStr,
                                                                                        entryPoint);
    bool isModule = jsPandaFile->IsModule(entryPoint);
    JSMutableHandle<JSTaggedValue> moduleNamespace(thread, JSTaggedValue::Undefined());
     if (!vm->GetModuleManager()->IsImportedModuleLoaded(moduleName.GetTaggedValue())) {
        if (!JSPandaFileExecutor::ExecuteFromFile(thread, fileNameStr.c_str(), entryPoint.c_str(), true)) {
            LOG_FULL(FATAL) << "Cannot execute dynamic-imported panda file : "<< fileNameStr.c_str()
                            << entryPoint.c_str();
         }
    }

    if (thread->HasPendingException()) {
        return CatchException(thread, reject);
    }
    if (!isModule) {
        moduleNamespace.Update(vm->GetGlobalEnv()->GetExportOfScript());
    } else {
        // b. Let moduleRecord be ! HostResolveImportedModule(referencingScriptOrModule, specifier).
        JSHandle<SourceTextModule> moduleRecord =
            vm->GetModuleManager()->HostGetImportedModule(moduleName.GetTaggedValue());
        // d. Let namespace be ? GetModuleNamespace(moduleRecord).
        moduleNamespace.Update(SourceTextModule::GetModuleNamespace(thread, moduleRecord));
    }
    if (thread->HasPendingException()) {
        return CatchException(thread, reject);
    }
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    EcmaRuntimeCallInfo *info =
        EcmaInterpreter::NewRuntimeCallInfo(thread,
                                            JSHandle<JSTaggedValue>(resolve),
                                            undefined, undefined, 1);
    info->SetCallArg(moduleNamespace.GetTaggedValue());
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    return JSFunction::Call(info);
}

JSTaggedValue BuiltinsPromiseJob::CatchException(JSThread *thread, JSHandle<JSPromiseReactionsFunction> reject)
{
    JSHandle<JSTaggedValue> undefined = thread->GlobalConstants()->GetHandledUndefined();
    ASSERT(thread->HasPendingException());
    JSHandle<JSTaggedValue> thenResult = JSPromise::IfThrowGetThrowValue(thread);
    thread->ClearException();
    JSHandle<JSTaggedValue> rejectfun(reject);
    EcmaRuntimeCallInfo *runtimeInfo =
        EcmaInterpreter::NewRuntimeCallInfo(thread, rejectfun, undefined, undefined, 1);
    RETURN_EXCEPTION_IF_ABRUPT_COMPLETION(thread);
    runtimeInfo->SetCallArg(thenResult.GetTaggedValue());
    return JSFunction::Call(runtimeInfo);
}
}  // namespace panda::ecmascript::builtins
