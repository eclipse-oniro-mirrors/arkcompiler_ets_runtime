/*
 * Copyright (c) 2022 Huawei Device Co., Ltd.
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

#include "ecmascript/jspandafile/js_pandafile_executor.h"

#include "ecmascript/base/path_helper.h"
#include "ecmascript/compiler/aot_file/an_file_data_manager.h"
#include "ecmascript/compiler/aot_file/aot_file_manager.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/jspandafile/program_object.h"
#include "ecmascript/mem/c_string.h"
#include "ecmascript/mem/c_containers.h"
#include "ecmascript/module/js_module_manager.h"
#include "ecmascript/patch/quick_fix_manager.h"

namespace panda::ecmascript {
using PathHelper = base::PathHelper;
Expected<JSTaggedValue, bool> JSPandaFileExecutor::ExecuteFromFile(JSThread *thread, const CString &filename,
    std::string_view entryPoint, bool needUpdate, bool excuteFromJob)
{
    LOG_ECMA(DEBUG) << "JSPandaFileExecutor::ExecuteFromFile filename " << filename;
    CString entry;
    CString name;
    CString normalName = PathHelper::NormalizePath(filename);
    EcmaVM *vm = thread->GetEcmaVM();
    if (!vm->IsBundlePack() && !excuteFromJob) {
#if defined(PANDA_TARGET_LINUX) || defined(OHOS_UNIT_TEST)
        name = filename;
        entry = entryPoint.data();
#else
        entry = PathHelper::ParseOhmUrl(vm, normalName, name);
#if !defined(PANDA_TARGET_WINDOWS) && !defined(PANDA_TARGET_MACOS)
        if (name.empty()) {
            name = vm->GetAssetPath();
        }
#elif defined(PANDA_TARGET_WINDOWS)
        CString assetPath = vm->GetAssetPath();
        name = assetPath + "\\" + JSPandaFile::MERGE_ABC_NAME;
#else
        CString assetPath = vm->GetAssetPath();
        name = assetPath + "/" + JSPandaFile::MERGE_ABC_NAME;
#endif
#endif
    } else {
        name = filename;
        entry = entryPoint.data();
    }

    std::shared_ptr<JSPandaFile> jsPandaFile =
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, name, entry, needUpdate);
    if (jsPandaFile == nullptr) {
        CString msg = "Load file with filename '" + name + "' failed, recordName '" + entry + "'";
        THROW_REFERENCE_ERROR_AND_RETURN(thread, msg.c_str(), Unexpected(false));
    }
    LoadAOTFilesForFile(vm, jsPandaFile.get());

    // realEntry is used to record the original record, which is easy to throw when there are exceptions
    const CString realEntry = entry;
    // If it is an old record, delete the bundleName and moduleName
    if (!jsPandaFile->IsBundlePack() && !excuteFromJob && !vm->GetBundleName().empty()) {
        jsPandaFile->CheckIsRecordWithBundleName(entry);
        if (!jsPandaFile->IsRecordWithBundleName()) {
            PathHelper::CroppingRecord(entry);
        }
    }

    bool isModule = jsPandaFile->IsModule(thread, entry, realEntry);
    if (thread->HasPendingException()) {
        vm->HandleUncaughtException(thread->GetException());
        return Unexpected(false);
    }
    if (isModule) {
        [[maybe_unused]] EcmaHandleScope scope(thread);
        ModuleManager *moduleManager = vm->GetModuleManager();
        JSHandle<JSTaggedValue> moduleRecord(thread->GlobalConstants()->GetHandledUndefined());
        if (jsPandaFile->IsBundlePack()) {
            moduleRecord = moduleManager->HostResolveImportedModule(name);
        } else {
            moduleRecord = moduleManager->HostResolveImportedModuleWithMerge(name, entry);
        }
        SourceTextModule::Instantiate(thread, moduleRecord);
        if (thread->HasPendingException()) {
            if (!excuteFromJob) {
                vm->HandleUncaughtException(thread->GetException());
            }
            return Unexpected(false);
        }
        JSHandle<SourceTextModule> module = JSHandle<SourceTextModule>::Cast(moduleRecord);
        module->SetStatus(ModuleStatus::INSTANTIATED);
        SourceTextModule::Evaluate(thread, module, nullptr, 0, excuteFromJob);
        return JSTaggedValue::Undefined();
    }
    return JSPandaFileExecutor::Execute(thread, jsPandaFile.get(), entry.c_str(), excuteFromJob);
}

// The security interface needs to be modified accordingly.
Expected<JSTaggedValue, bool> JSPandaFileExecutor::ExecuteFromBuffer(JSThread *thread,
    const void *buffer, size_t size, std::string_view entryPoint, const CString &filename, bool needUpdate)
{
    LOG_ECMA(DEBUG) << "JSPandaFileExecutor::ExecuteFromBuffer filename " << filename;
    CString normalName = PathHelper::NormalizePath(filename);
    std::shared_ptr<JSPandaFile> jsPandaFile =
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, normalName, entryPoint, buffer, size, needUpdate);
    if (jsPandaFile == nullptr) {
        CString msg = "Load file with filename '" + normalName + "' failed, recordName '" + entryPoint.data() + "'";
        THROW_REFERENCE_ERROR_AND_RETURN(thread, msg.c_str(), Unexpected(false));
    }
    auto vm = thread->GetEcmaVM();
    LoadAOTFilesForFile(vm, jsPandaFile.get());

    CString entry = entryPoint.data();
    bool isModule = jsPandaFile->IsModule(thread, entry);
    if (isModule) {
        bool isBundle = jsPandaFile->IsBundlePack();
        return CommonExecuteBuffer(thread, isBundle, normalName, entry, buffer, size);
    }
    return JSPandaFileExecutor::Execute(thread, jsPandaFile.get(), entry);
}

// The security interface needs to be modified accordingly.
Expected<JSTaggedValue, bool> JSPandaFileExecutor::ExecuteModuleBuffer(
    JSThread *thread, const void *buffer, size_t size, const CString &filename, bool needUpdate)
{
    LOG_ECMA(DEBUG) << "JSPandaFileExecutor::ExecuteModuleBuffer filename " << filename;
    CString name;
    EcmaVM *vm = thread->GetEcmaVM();
#if !defined(PANDA_TARGET_WINDOWS) && !defined(PANDA_TARGET_MACOS)
    name = vm->GetAssetPath();
#elif defined(PANDA_TARGET_WINDOWS)
    CString assetPath = vm->GetAssetPath();
    name = assetPath + "\\" + JSPandaFile::MERGE_ABC_NAME;
#else
    CString assetPath = vm->GetAssetPath();
    name = assetPath + "/" + JSPandaFile::MERGE_ABC_NAME;
#endif
    CString normalName = PathHelper::NormalizePath(filename);
    CString entry = PathHelper::ParseOhmUrl(vm, normalName, name);
    std::shared_ptr<JSPandaFile> jsPandaFile =
        JSPandaFileManager::GetInstance()->LoadJSPandaFile(thread, name, entry, buffer, size, needUpdate);
    if (jsPandaFile == nullptr) {
        CString msg = "Load file with filename '" + name + "' failed, recordName '" + entry + "'";
        THROW_REFERENCE_ERROR_AND_RETURN(thread, msg.c_str(), Unexpected(false));
    }
    LoadAOTFilesForFile(vm, jsPandaFile.get());

    bool isBundle = jsPandaFile->IsBundlePack();

    // realEntry is used to record the original record, which is easy to throw when there are exceptions
    const CString realEntry = entry;
    if (!isBundle) {
        jsPandaFile->CheckIsRecordWithBundleName(entry);
        if (!jsPandaFile->IsRecordWithBundleName()) {
            PathHelper::CroppingRecord(entry);
        }
    }
    // will be refactored, temporarily use the function IsModule to verify realEntry
    [[maybe_unused]] bool isModule = jsPandaFile->IsModule(thread, entry, realEntry);
    if (thread->HasPendingException()) {
        vm->HandleUncaughtException(thread->GetException());
        return Unexpected(false);
    }
    ASSERT(isModule);
    return CommonExecuteBuffer(thread, isBundle, name, entry, buffer, size);
}

// The security interface needs to be modified accordingly.
Expected<JSTaggedValue, bool> JSPandaFileExecutor::CommonExecuteBuffer(JSThread *thread,
    bool isBundle, const CString &filename, const CString &entry, const void *buffer, size_t size)
{
    [[maybe_unused]] EcmaHandleScope scope(thread);
    EcmaVM *vm = thread->GetEcmaVM();
    ModuleManager *moduleManager = vm->GetModuleManager();
    moduleManager->SetExecuteMode(true);
    JSMutableHandle<JSTaggedValue> moduleRecord(thread, thread->GlobalConstants()->GetUndefined());
    if (isBundle) {
        moduleRecord.Update(moduleManager->HostResolveImportedModule(buffer, size, filename));
    } else {
        moduleRecord.Update(moduleManager->HostResolveImportedModuleWithMerge(filename, entry));
    }

    SourceTextModule::Instantiate(thread, moduleRecord);
    if (thread->HasPendingException()) {
        vm->HandleUncaughtException(thread->GetException());
        return Unexpected(false);
    }

    JSHandle<SourceTextModule> module = JSHandle<SourceTextModule>::Cast(moduleRecord);
    module->SetStatus(ModuleStatus::INSTANTIATED);
    SourceTextModule::Evaluate(thread, module, buffer, size);
    return JSTaggedValue::Undefined();
}

Expected<JSTaggedValue, bool> JSPandaFileExecutor::Execute(JSThread *thread, const JSPandaFile *jsPandaFile,
                                                           std::string_view entryPoint, bool excuteFromJob)
{
    // For Ark application startup
    EcmaVM *vm = thread->GetEcmaVM();

    QuickFixManager *quickFixManager = vm->GetQuickFixManager();
    quickFixManager->LoadPatchIfNeeded(thread, jsPandaFile);

    Expected<JSTaggedValue, bool> result = vm->InvokeEcmaEntrypoint(jsPandaFile, entryPoint, excuteFromJob);
    return result;
}

void JSPandaFileExecutor::LoadAOTFilesForFile(EcmaVM *vm, JSPandaFile *jsPandaFile)
{
    if (vm->GetJSOptions().GetEnableAsmInterpreter()) {
        auto aotFM = vm->GetAOTFileManager();
        if (vm->GetJSOptions().WasAOTOutputFileSet()) {
            AnFileDataManager::GetInstance()->SetEnable(true);
            std::string aotFilename = vm->GetJSOptions().GetAOTOutputFile();
            vm->LoadAOTFiles(aotFilename);
        }
        if (aotFM->IsLoad(jsPandaFile)) {
            uint32_t index = aotFM->GetAnFileIndex(jsPandaFile);
            jsPandaFile->SetAOTFileInfoIndex(index);
        }
    }
}

Expected<JSTaggedValue, bool> JSPandaFileExecutor::ExecuteFromBufferSecure(JSThread *thread, uint8_t *buffer,
    size_t size, std::string_view entryPoint, const CString &filename, bool needUpdate)
{
    LOG_ECMA(DEBUG) << "JSPandaFileExecutor::ExecuteFromBufferSecure with secure buffer filename " << filename;
    CString normalName = PathHelper::NormalizePath(filename);
    std::shared_ptr<JSPandaFile> jsPandaFile = JSPandaFileManager::GetInstance()->
        LoadJSPandaFileSecure(thread, normalName, entryPoint, buffer, size, needUpdate);
    if (jsPandaFile == nullptr) {
        CString msg = "Load file with filename '" + normalName + "' failed, recordName '" + entryPoint.data() + "'";
        THROW_REFERENCE_ERROR_AND_RETURN(thread, msg.c_str(), Unexpected(false));
    }
    auto vm = thread->GetEcmaVM();
    LoadAOTFilesForFile(vm, jsPandaFile.get());

    CString entry = entryPoint.data();
    bool isModule = jsPandaFile->IsModule(thread, entry);
    if (isModule) {
        return CommonExecuteBuffer(thread, normalName, entry, jsPandaFile.get());
    }
    return JSPandaFileExecutor::Execute(thread, jsPandaFile.get(), entry);
}

Expected<JSTaggedValue, bool> JSPandaFileExecutor::CommonExecuteBuffer(JSThread *thread, const CString &filename,
    const CString &entry, const JSPandaFile *jsPandaFile)
{
    [[maybe_unused]] EcmaHandleScope scope(thread);
    EcmaVM *vm = thread->GetEcmaVM();
    ModuleManager *moduleManager = vm->GetModuleManager();
    moduleManager->SetExecuteMode(true);
    JSMutableHandle<JSTaggedValue> moduleRecord(thread, thread->GlobalConstants()->GetUndefined());
    if (jsPandaFile->IsBundlePack()) {
        moduleRecord.Update(moduleManager->HostResolveImportedModule(jsPandaFile, filename));
    } else {
        moduleRecord.Update(moduleManager->HostResolveImportedModuleWithMerge(filename, entry));
    }

    SourceTextModule::Instantiate(thread, moduleRecord);
    if (thread->HasPendingException()) {
        vm->HandleUncaughtException(thread->GetException());
        return Unexpected(false);
    }

    JSHandle<SourceTextModule> module = JSHandle<SourceTextModule>::Cast(moduleRecord);
    module->SetStatus(ModuleStatus::INSTANTIATED);
    SourceTextModule::Evaluate(thread, module, nullptr, 0);
    return JSTaggedValue::Undefined();
}

Expected<JSTaggedValue, bool> JSPandaFileExecutor::ExecuteModuleBufferSecure(JSThread *thread, uint8_t *buffer,
    size_t size, const CString &filename, bool needUpdate)
{
    LOG_ECMA(DEBUG) << "JSPandaFileExecutor::ExecuteModuleBufferSecure with secure buffer filename " << filename;
    CString name;
    EcmaVM *vm = thread->GetEcmaVM();
#if !defined(PANDA_TARGET_WINDOWS) && !defined(PANDA_TARGET_MACOS)
    name = vm->GetAssetPath();
#elif defined(PANDA_TARGET_WINDOWS)
    CString assetPath = vm->GetAssetPath();
    name = assetPath + "\\" + JSPandaFile::MERGE_ABC_NAME;
#else
    CString assetPath = vm->GetAssetPath();
    name = assetPath + "/" + JSPandaFile::MERGE_ABC_NAME;
#endif
    CString normalName = PathHelper::NormalizePath(filename);
    CString entry = PathHelper::ParseOhmUrl(vm, normalName, name);
    std::shared_ptr<JSPandaFile> jsPandaFile = JSPandaFileManager::GetInstance()->
        LoadJSPandaFileSecure(thread, name, entry, buffer, size, needUpdate);
    if (jsPandaFile == nullptr) {
        CString msg = "Load file with filename '" + name + "' failed, recordName '" + entry + "'";
        THROW_REFERENCE_ERROR_AND_RETURN(thread, msg.c_str(), Unexpected(false));
    }
    LoadAOTFilesForFile(vm, jsPandaFile.get());

    // realEntry is used to record the original record, which is easy to throw when there are exceptions
    const CString realEntry = entry;
    if (!jsPandaFile->IsBundlePack()) {
        jsPandaFile->CheckIsRecordWithBundleName(entry);
        if (!jsPandaFile->IsRecordWithBundleName()) {
            PathHelper::CroppingRecord(entry);
        }
    }

    // will be refactored, temporarily use the function IsModule to verify realEntry
    [[maybe_unused]] bool isModule = jsPandaFile->IsModule(thread, entry, realEntry);
    if (thread->HasPendingException()) {
        vm->HandleUncaughtException(thread->GetException());
        return Unexpected(false);
    }
    ASSERT(isModule);
    return CommonExecuteBuffer(thread, name, entry, jsPandaFile.get());
}
}  // namespace panda::ecmascript
