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

#ifndef ECMASCRIPT_MODULE_JS_MODULE_MANAGER_H
#define ECMASCRIPT_MODULE_JS_MODULE_MANAGER_H

#include "ecmascript/js_tagged_value-inl.h"
#include "ecmascript/jspandafile/js_pandafile.h"
#include "ecmascript/module/js_module_source_text.h"
#include "ecmascript/napi/jsnapi_helper.h"
#include "ecmascript/tagged_dictionary.h"

namespace panda::ecmascript {
class ModuleManager {
public:
    explicit ModuleManager(EcmaVM *vm);
    ~ModuleManager() = default;

    JSTaggedValue GetModuleValueInner(int32_t index);
    JSTaggedValue GetModuleValueInner(int32_t index, JSTaggedValue jsFunc);
    JSTaggedValue GetModuleValueInner(int32_t index, JSHandle<JSTaggedValue> currentModule);
    JSTaggedValue GetModuleValueOutter(int32_t index);
    JSTaggedValue GetModuleValueOutter(int32_t index, JSTaggedValue jsFunc);
    JSTaggedValue GetModuleValueOutter(int32_t index, JSHandle<JSTaggedValue> currentModule);
    void StoreModuleValue(int32_t index, JSTaggedValue value);
    void StoreModuleValue(int32_t index, JSTaggedValue value, JSTaggedValue jsFunc);
    JSTaggedValue GetModuleNamespace(int32_t index);
    JSTaggedValue GetModuleNamespace(int32_t index, JSTaggedValue currentFunc);
    JSTaggedValue GetModuleNamespaceInternal(int32_t index, JSTaggedValue currentModule);

    // deprecated begin
    JSTaggedValue GetModuleValueInner(JSTaggedValue key);
    JSTaggedValue GetModuleValueInner(JSTaggedValue key, JSTaggedValue jsFunc);
    JSTaggedValue GetModuleValueOutter(JSTaggedValue key);
    JSTaggedValue GetModuleValueOutter(JSTaggedValue key, JSTaggedValue jsFunc);
    void StoreModuleValue(JSTaggedValue key, JSTaggedValue value);
    void StoreModuleValue(JSTaggedValue key, JSTaggedValue value, JSTaggedValue jsFunc);
    JSTaggedValue GetModuleNamespace(JSTaggedValue localName);
    JSTaggedValue GetModuleNamespace(JSTaggedValue localName, JSTaggedValue currentFunc);
    JSTaggedValue GetModuleNamespaceInternal(JSTaggedValue localName, JSTaggedValue currentModule);
    // deprecated end

    JSHandle<SourceTextModule> PUBLIC_API HostGetImportedModule(const CString &referencingModule);
    JSHandle<SourceTextModule> HostGetImportedModule(JSTaggedValue referencing);
    JSTaggedValue HostGetImportedModule(void *src);
    bool IsImportedModuleLoaded(JSTaggedValue referencing);

    JSHandle<JSTaggedValue> ResolveNativeModule(const CString &moduleRequestName, ModuleTypes moduleType);
    JSHandle<JSTaggedValue> HostResolveImportedModule(const void *buffer, size_t size, const CString &filename);
    JSHandle<JSTaggedValue> HostResolveImportedModule(const CString &referencingModule,
        bool executeFromJob = false);
    JSHandle<JSTaggedValue> PUBLIC_API HostResolveImportedModuleWithMerge(const CString &referencingModule,
        const CString &recordName, bool executeFromJob = false);
    JSHandle<JSTaggedValue> PUBLIC_API HostResolveImportedModuleWithMergeForHotReload(const CString &referencingModule,
        const CString &recordName, bool executeFromJob = false);
    JSHandle<JSTaggedValue> HostResolveImportedModule(const JSPandaFile *jsPandaFile, const CString &filename);

    JSHandle<JSTaggedValue> LoadNativeModule(JSThread *thread, const std::string &key);
    JSHandle<JSTaggedValue> GetModuleNameSpaceFromFile(
        JSThread *thread, std::string &recordNameStr, std::string &baseFileName);

    JSTaggedValue GetCurrentModule();
    JSHandle<JSTaggedValue> GenerateFuncModule(const JSPandaFile *jsPandaFile, const CString &entryPoint,
                                               ClassKind classKind = ClassKind::NON_SENDABLE);
    JSHandle<JSTaggedValue> GenerateSendableFuncModule(const JSHandle<JSTaggedValue> &module);
    JSTaggedValue GetNativeModuleValue(JSThread *thread, JSTaggedValue currentModule,
        JSTaggedValue resolvedModule, ResolvedIndexBinding *binding);
    JSTaggedValue GetNativeModuleValue(JSThread *thread, JSTaggedValue currentModule,
        JSTaggedValue resolvedModule, ResolvedBinding *binding);
    JSTaggedValue GetCJSModuleValue(JSThread *thread, JSTaggedValue currentModule,
        JSTaggedValue resolvedModule, ResolvedIndexBinding *binding);
    void AddResolveImportedModule(const JSPandaFile *jsPandaFile, const CString &referencingModule);
    void AddResolveImportedModule(const CString &referencingModule, JSHandle<JSTaggedValue> moduleRecord);
    void Iterate(const RootVisitor &v);

    bool GetExecuteMode() const
    {
        return isExecuteBuffer_;
    }
    void SetExecuteMode(bool mode)
    {
        isExecuteBuffer_ = mode;
    }

    static CString PUBLIC_API GetRecordName(JSTaggedValue module);
    static int GetExportObjectIndex(EcmaVM *vm, JSHandle<SourceTextModule> ecmaModule, const std::string &key);

    uint32_t NextModuleAsyncEvaluatingOrdinal()
    {
        uint32_t ordinal = nextModuleAsyncEvaluatingOrdinal_++;
        return ordinal;
    }

private:
    NO_COPY_SEMANTIC(ModuleManager);
    NO_MOVE_SEMANTIC(ModuleManager);

    bool SkipDefaultBundleFile(const CString &moduleFileName) const;
    JSHandle<JSTaggedValue> ResolveModuleInMergedABC(JSThread *thread, const JSPandaFile *jsPandaFile,
        const CString &recordName, bool executeFromJob = false);
    JSHandle<JSTaggedValue> CreateEmptyModule();
    JSTaggedValue GetModuleValueOutterInternal(int32_t index, JSTaggedValue currentModule);
    void StoreModuleValueInternal(JSHandle<SourceTextModule> &currentModule,
                                  int32_t index, JSTaggedValue value);

    JSTaggedValue GetValueFromExportObject(JSHandle<JSTaggedValue> &exportObject, int32_t index);

    // deprecated begin
    JSTaggedValue GetModuleValueOutterInternal(JSTaggedValue key, JSTaggedValue currentModule);
    void StoreModuleValueInternal(JSHandle<SourceTextModule> &currentModule,
                                  JSTaggedValue key, JSTaggedValue value);
    // deprecated end

    JSHandle<JSTaggedValue> ResolveModule(JSThread *thread, const JSPandaFile *jsPandaFile,
        bool executeFromJob = false);
    JSHandle<JSTaggedValue> ResolveModuleWithMerge(JSThread *thread, const JSPandaFile *jsPandaFile,
        const CString &recordName, bool executeFromJob = false);

    JSHandle<JSTaggedValue> CommonResolveImportedModuleWithMerge(const CString &moduleFileName,
        const CString &recordName, bool executeFromJob = false);

    static constexpr uint32_t DEAULT_DICTIONART_CAPACITY = 4;

    uint32_t nextModuleAsyncEvaluatingOrdinal_{SourceTextModule::FIRST_ASYNC_EVALUATING_ORDINAL};

    EcmaVM *vm_ {nullptr};
    JSTaggedValue resolvedModules_ {JSTaggedValue::Hole()};
    JSTaggedValue cachedEmptyModule_ {JSTaggedValue::Hole()};
    bool isExecuteBuffer_ {false};

    friend class EcmaVM;
    friend class PatchLoader;
    friend class ModuleDeregister;
};
} // namespace panda::ecmascript
#endif // ECMASCRIPT_MODULE_JS_MODULE_MANAGER_H
