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

#include "ecmascript/module/module_resolver.h"

#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/module/js_shared_module_manager.h"
#include "ecmascript/module/module_path_helper.h"
#include "ecmascript/module/js_module_deregister.h"
#include "ecmascript/module/module_data_extractor.h"
#include "ecmascript/object_fast_operator-inl.h"
#include "ecmascript/patch/quick_fix_manager.h"

namespace panda::ecmascript {
JSHandle<JSTaggedValue> ModuleResolver::HostResolveImportedModule(JSThread *thread,
                                                                  const JSHandle<SourceTextModule> &module,
                                                                  const JSHandle<JSTaggedValue> &moduleRequest,
                                                                  bool executeFromJob)
{
    return module->GetEcmaModuleRecordNameString().empty() ?
        HostResolveImportedModuleBundlePack(thread, module, moduleRequest, executeFromJob) :
        HostResolveImportedModuleWithMerge(thread, module, moduleRequest, executeFromJob);
}

JSHandle<JSTaggedValue> ModuleResolver::HostResolveImportedModule(JSThread* thread,
                                                                  const CString& fileName,
                                                                  const CString& recordName,
                                                                  const JSPandaFile* jsPandaFile,
                                                                  bool executeFromJob)
{
    if (jsPandaFile == nullptr) {
        std::shared_ptr<JSPandaFile> file =
            JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, fileName, recordName, false);
        RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
        if (file == nullptr) {
            CString msg = "Load file with filename '" + fileName + "' failed, recordName '" + recordName + "'";
            THROW_NEW_ERROR_AND_RETURN_HANDLE(thread, ErrorType::REFERENCE_ERROR, JSTaggedValue, msg.c_str());
        }
        jsPandaFile = file.get();
    }
    return jsPandaFile->IsBundlePack() ?
        HostResolveImportedModuleBundlePack(thread, fileName, executeFromJob) :
        HostResolveImportedModuleWithMerge(thread, fileName, recordName, jsPandaFile, executeFromJob);
}

JSHandle<JSTaggedValue> ModuleResolver::HostResolveImportedModule(JSThread* thread,
                                                                  const CString& fileName,
                                                                  const CString& recordName,
                                                                  const void* buffer,
                                                                  size_t size,
                                                                  bool executeFromJob)
{
    std::shared_ptr<JSPandaFile> jsPandaFile =
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, fileName, recordName, buffer, size);
    RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
    if (jsPandaFile == nullptr) {
        CString msg = "Load file with filename '" + fileName + "' failed, recordName '" + recordName + "'";
        THROW_NEW_ERROR_AND_RETURN_HANDLE(thread, ErrorType::REFERENCE_ERROR, JSTaggedValue, msg.c_str());
    }
    return jsPandaFile->IsBundlePack() ?
        HostResolveImportedModuleBundlePackBuffer(thread, fileName, jsPandaFile.get(), executeFromJob) :
        HostResolveImportedModuleWithMerge(thread, fileName, recordName, jsPandaFile.get(), executeFromJob);
}

JSHandle<JSTaggedValue> ModuleResolver::HostResolveImportedModuleWithMerge(JSThread *thread,
                                                                           const JSHandle<SourceTextModule> &module,
                                                                           const JSHandle<JSTaggedValue> &moduleRequest,
                                                                           bool executeFromJob)
{
    CString moduleRequestName = ModulePathHelper::Utf8ConvertToString(moduleRequest.GetTaggedValue());
    ReplaceModuleThroughFeature(thread, moduleRequestName);

    CString baseFilename{};
    StageOfHotReload stageOfHotReload = thread->GetCurrentEcmaContext()->GetStageOfHotReload();
    if (stageOfHotReload == StageOfHotReload::BEGIN_EXECUTE_PATCHMAIN ||
        stageOfHotReload == StageOfHotReload::LOAD_END_EXECUTE_PATCHMAIN) {
        baseFilename = thread->GetEcmaVM()->GetQuickFixManager()->GetBaseFileName(module);
    } else {
        baseFilename = module->GetEcmaModuleFilenameString();
    }

    auto moduleManager = thread->GetCurrentEcmaContext()->GetModuleManager();
    auto [isNative, moduleType] = SourceTextModule::CheckNativeModule(moduleRequestName);
    if (isNative) {
        if (moduleManager->IsLocalModuleLoaded(moduleRequestName)) {
            return JSHandle<JSTaggedValue>(moduleManager->HostGetImportedModule(moduleRequestName));
        }
        return ResolveNativeModule(thread, moduleRequestName, baseFilename, moduleType);
    }
    CString recordName = module->GetEcmaModuleRecordNameString();
    std::shared_ptr<JSPandaFile> pandaFile =
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, baseFilename, recordName);
    if (pandaFile == nullptr) { // LCOV_EXCL_BR_LINE
        LOG_FULL(FATAL) << "Load current file's panda file failed. Current file is " << baseFilename;
    }

    CString entryPoint =
        ModulePathHelper::ConcatFileNameWithMerge(thread, pandaFile.get(), baseFilename, recordName, moduleRequestName);
    RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);

#if defined(PANDA_TARGET_WINDOWS) || defined(PANDA_TARGET_MACOS)
    if (entryPoint == ModulePathHelper::PREVIEW_OF_ACROSS_HAP_FLAG) {
        THROW_SYNTAX_ERROR_AND_RETURN(thread, "", thread->GlobalConstants()->GetHandledUndefined());
    }
#endif
    return HostResolveImportedModuleWithMerge(thread, baseFilename, entryPoint, nullptr, executeFromJob);
}

JSHandle<JSTaggedValue> ModuleResolver::HostResolveImportedModuleBundlePack(JSThread *thread,
    const JSHandle<SourceTextModule> &module,
    const JSHandle<JSTaggedValue> &moduleRequest,
    bool executeFromJob)
{
    auto moduleManager = thread->GetCurrentEcmaContext()->GetModuleManager();
    CString moduleRequestStr = ModulePathHelper::Utf8ConvertToString(moduleRequest.GetTaggedValue());
    if (moduleManager->IsLocalModuleLoaded(moduleRequestStr)) {
        return JSHandle<JSTaggedValue>(moduleManager->HostGetImportedModule(moduleRequestStr));
    }

    CString dirname = base::PathHelper::ResolveDirPath(module->GetEcmaModuleFilenameString());
    CString moduleFilename = ResolveFilenameFromNative(thread, dirname, moduleRequestStr);
    RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
    return HostResolveImportedModuleBundlePack(thread, moduleFilename, executeFromJob);
}
void ModuleResolver::ReplaceModuleThroughFeature(JSThread *thread, CString &requestName)
{
    const auto vm = thread->GetEcmaVM();
    // check if module need to be mock
    if (vm->IsMockModule(requestName)) {
        requestName = vm->GetMockModule(requestName);
    }

    // Load the replaced module, hms -> system hsp
    if (vm->IsHmsModule(requestName)) {
        requestName = vm->GetHmsModule(requestName);
    }
}

JSHandle<JSTaggedValue> ModuleResolver::ResolveSharedImportedModuleWithMerge(JSThread *thread,
                                                                             const CString &fileName,
                                                                             const CString &recordName,
                                                                             const JSPandaFile *jsPandaFile,
                                                                             [[maybe_unused]] JSRecordInfo *recordInfo)
{
    auto sharedModuleManager = SharedModuleManager::GetInstance();
    if (sharedModuleManager->SearchInSModuleManager(thread, recordName)) {
        return JSHandle<JSTaggedValue>(sharedModuleManager->GetSModule(thread, recordName));
    }
    // before resolving module completely, shared-module put into isolate -thread resolvedModules_ temporarily.
    ModuleManager *moduleManager = thread->GetCurrentEcmaContext()->GetModuleManager();
    JSHandle<JSTaggedValue> module = moduleManager->TryGetImportedModule(recordName);
    if (!module->IsUndefined()) {
        return module;
    }

    ASSERT(jsPandaFile->IsModule(recordInfo));
    JSHandle<JSTaggedValue> moduleRecord =
        SharedModuleHelper::ParseSharedModule(thread, jsPandaFile, recordName, fileName, recordInfo);
    JSHandle<SourceTextModule>::Cast(moduleRecord)->SetEcmaModuleRecordNameString(recordName);
    moduleManager->AddResolveImportedModule(recordName, moduleRecord.GetTaggedValue());
    moduleManager->AddToInstantiatingSModuleList(recordName);
    return moduleRecord;
}

JSHandle<JSTaggedValue> ModuleResolver::HostResolveImportedModuleForHotReload(JSThread *thread,
                                                                              const CString &moduleFileName,
                                                                              const CString &recordName,
                                                                              bool executeFromJob)
{
    std::shared_ptr<JSPandaFile> jsPandaFile =
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, moduleFileName, recordName, false);
    if (jsPandaFile == nullptr) { // LCOV_EXCL_BR_LINE
        LOG_FULL(FATAL) << "Load current file's panda file failed. Current file is " << moduleFileName;
    }
    JSRecordInfo *recordInfo = nullptr;
    bool hasRecord = jsPandaFile->CheckAndGetRecordInfo(recordName, &recordInfo);
    if (!hasRecord) {
        CString msg = "cannot find record '" + recordName + "',please check the request path.'" + moduleFileName + "'.";
        THROW_NEW_ERROR_AND_RETURN_HANDLE(thread, ErrorType::REFERENCE_ERROR, JSTaggedValue, msg.c_str());
    }
    JSHandle<JSTaggedValue> moduleRecord =
        ResolveModuleWithMerge(thread, jsPandaFile.get(), recordName, recordInfo, executeFromJob);
    RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
    ModuleManager *moduleManager = thread->GetCurrentEcmaContext()->GetModuleManager();
    moduleManager->UpdateResolveImportedModule(recordName, moduleRecord.GetTaggedValue());
    return moduleRecord;
}

JSHandle<JSTaggedValue> ModuleResolver::HostResolveImportedModuleWithMerge(JSThread *thread,
                                                                           const CString &moduleFileName,
                                                                           const CString &recordName,
                                                                           const JSPandaFile *jsPandaFile,
                                                                           bool executeFromJob)
{
    if (jsPandaFile == nullptr) {
        std::shared_ptr<JSPandaFile> file =
            JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, moduleFileName, recordName, false);
        RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
        if (file == nullptr) {
            CString msg = "Load file with filename '" + moduleFileName + "' failed, recordName '" + recordName + "'";
            THROW_NEW_ERROR_AND_RETURN_HANDLE(thread, ErrorType::REFERENCE_ERROR, JSTaggedValue, msg.c_str());
        }
        jsPandaFile = file.get();
    }
    JSRecordInfo *recordInfo = nullptr;
    bool hasRecord = jsPandaFile->CheckAndGetRecordInfo(recordName, &recordInfo);
    if (!hasRecord) {
        CString msg = "cannot find record '" + recordName + "',please check the request path.'" + moduleFileName + "'.";
        THROW_NEW_ERROR_AND_RETURN_HANDLE(thread, ErrorType::REFERENCE_ERROR, JSTaggedValue, msg.c_str());
    }

    if (jsPandaFile->IsSharedModule(recordInfo)) {
        return ResolveSharedImportedModuleWithMerge(thread, moduleFileName, recordName, jsPandaFile, recordInfo);
    }
    ModuleManager *moduleManager = thread->GetCurrentEcmaContext()->GetModuleManager();
    JSHandle<JSTaggedValue> module = moduleManager->TryGetImportedModule(recordName);
    if (!module->IsUndefined()) {
        return module;
    }
    JSHandle<JSTaggedValue> moduleRecord =
        ResolveModuleWithMerge(thread, jsPandaFile, recordName, recordInfo, executeFromJob);
    RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
    moduleManager->AddResolveImportedModule(recordName, moduleRecord.GetTaggedValue());
    return moduleRecord;
}
JSHandle<JSTaggedValue> ModuleResolver::HostResolveImportedModuleBundlePackBuffer(JSThread *thread,
                                                                                  const CString &referencingModule,
                                                                                  const JSPandaFile *jsPandaFile,
                                                                                  bool executeFromJob)
{
    ModuleManager *moduleManager = thread->GetCurrentEcmaContext()->GetModuleManager();
    JSHandle<JSTaggedValue> module = moduleManager->TryGetImportedModule(referencingModule);
    if (!module->IsUndefined()) {
        return module;
    }
    return ResolveModuleBundlePack(thread, jsPandaFile, executeFromJob);
}
JSHandle<JSTaggedValue> ModuleResolver::HostResolveImportedModuleBundlePack(JSThread *thread,
                                                                            const CString &referencingModule,
                                                                            bool executeFromJob)
{
    ModuleManager *moduleManager = thread->GetCurrentEcmaContext()->GetModuleManager();
    // Can not use jsPandaFile from js_pandafile_executor, need to construct with JSPandaFile::ENTRY_MAIN_FUNCTION
    std::shared_ptr<JSPandaFile> jsPandaFile =
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, referencingModule, JSPandaFile::ENTRY_MAIN_FUNCTION);
    if (jsPandaFile == nullptr) { // LCOV_EXCL_BR_LINE
        LOG_FULL(FATAL) << "Load current file's panda file failed. Current file is " << referencingModule;
    }
    [[maybe_unused]] JSRecordInfo *recordInfo = nullptr;
    ASSERT(jsPandaFile->CheckAndGetRecordInfo(referencingModule, &recordInfo) &&
        !jsPandaFile->IsSharedModule(recordInfo));
    CString moduleFileName = referencingModule;
    if (moduleManager->IsVMBundlePack()) {
        if (!AOTFileManager::GetAbsolutePath(referencingModule, moduleFileName)) {
            CString msg = "Parse absolute " + referencingModule + " path failed";
            THROW_NEW_ERROR_AND_RETURN_HANDLE(thread, ErrorType::REFERENCE_ERROR, JSTaggedValue, msg.c_str());
        }
    }
    JSHandle<JSTaggedValue> module = moduleManager->TryGetImportedModule(moduleFileName);
    if (!module->IsUndefined()) {
        return module;
    }
    std::shared_ptr<JSPandaFile> pandaFile =
           JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, moduleFileName, JSPandaFile::ENTRY_MAIN_FUNCTION);
    if (pandaFile == nullptr) { // LCOV_EXCL_BR_LINE
        LOG_FULL(FATAL) << "Load current file's panda file failed. Current file is " << referencingModule;
    }

    return ResolveModuleBundlePack(thread, pandaFile.get(), executeFromJob);
}

JSHandle<JSTaggedValue> ModuleResolver::ResolveModuleBundlePack(JSThread *thread,
                                                                const JSPandaFile *jsPandaFile,
                                                                bool executeFromJob)
{
    CString moduleFileName = jsPandaFile->GetJSPandaFileDesc();
    JSHandle<JSTaggedValue> moduleRecord = thread->GlobalConstants()->GetHandledUndefined();
    JSRecordInfo recordInfo = const_cast<JSPandaFile *>(jsPandaFile)->FindRecordInfo(JSPandaFile::ENTRY_FUNCTION_NAME);
    if (jsPandaFile->IsModule(&recordInfo)) {
        moduleRecord =
            ModuleDataExtractor::ParseModule(thread, jsPandaFile, moduleFileName, moduleFileName, &recordInfo);
    } else {
        ASSERT(jsPandaFile->IsCjs(&recordInfo));
        moduleRecord = ModuleDataExtractor::ParseCjsModule(thread, jsPandaFile);
    }
    // json file can not be compiled into isolate abc.
    ASSERT(!jsPandaFile->IsJson(&recordInfo));
    ModuleDeregister::InitForDeregisterModule(moduleRecord, executeFromJob);
    ModuleManager *moduleManager = thread->GetCurrentEcmaContext()->GetModuleManager();
    moduleManager->AddResolveImportedModule(moduleFileName, moduleRecord.GetTaggedValue());
    return moduleRecord;
}

JSHandle<JSTaggedValue> ModuleResolver::ResolveNativeModule(JSThread *thread,
                                                            const CString &moduleRequest,
                                                            const CString &baseFileName,
                                                            ModuleTypes moduleType)
{
    JSHandle<JSTaggedValue> moduleRecord =
        ModuleDataExtractor::ParseNativeModule(thread, moduleRequest, baseFileName, moduleType);
    ModuleManager *moduleManager = thread->GetCurrentEcmaContext()->GetModuleManager();
    moduleManager->AddResolveImportedModule(moduleRequest, moduleRecord.GetTaggedValue());
    return moduleRecord;
}

JSHandle<JSTaggedValue> ModuleResolver::ResolveModuleWithMerge(JSThread *thread,
                                                               const JSPandaFile *jsPandaFile,
                                                               const CString &recordName,
                                                               JSRecordInfo *recordInfo,
                                                               bool executeFromJob)
{
    CString moduleFileName = jsPandaFile->GetJSPandaFileDesc();
    JSHandle<JSTaggedValue> moduleRecord = thread->GlobalConstants()->GetHandledUndefined();
    if (jsPandaFile->IsModule(recordInfo)) {
        RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
        moduleRecord = ModuleDataExtractor::ParseModule(thread, jsPandaFile, recordName, moduleFileName, recordInfo);
    } else if (jsPandaFile->IsJson(recordInfo)) {
        moduleRecord = ModuleDataExtractor::ParseJsonModule(thread, jsPandaFile, moduleFileName, recordName);
    } else {
        ASSERT(jsPandaFile->IsCjs(recordInfo));
        RETURN_HANDLE_IF_ABRUPT_COMPLETION(JSTaggedValue, thread);
        moduleRecord = ModuleDataExtractor::ParseCjsModule(thread, jsPandaFile);
    }

    JSHandle<SourceTextModule>::Cast(moduleRecord)->SetEcmaModuleRecordNameString(recordName);
    ModuleDeregister::InitForDeregisterModule(moduleRecord, executeFromJob);
    return moduleRecord;
}
} // namespace panda::ecmascript