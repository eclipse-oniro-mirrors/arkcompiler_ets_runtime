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

#include "constpool_snapshot.h"

#include "ecmascript/base/config.h"
#include "ecmascript/log_wrapper.h"
#include "ecmascript/module/module_path_helper.h"
#include "ecmascript/platform/file.h"
#include "ecmascript/platform/signal_manager.h"
#include "ecmascript/serializer/file_deserializer.h"
#include "ecmascript/serializer/file_serializer.h"
#include "ecmascript/snapshot/common/modules_snapshot_helper.h"
#include "ecmascript/jspandafile/program_object.h"

#include <securec.h>
#include <zlib.h>
#include <cstdint>
#include <string>

using IndexHeader = panda::panda_file::File::IndexHeader;

namespace panda::ecmascript {
namespace {
// Per-type counters for restored constpool cache slots, collected only when snapshot stats are on.
struct ConstPoolCacheStats {
    uint32_t cacheSlots = 0;
    uint32_t stringSlots = 0;
    uint32_t methodSlots = 0;
    uint32_t otherSlots = 0;
};

// Classify restored cache slots by object type (counted before publishing the pools).
// The serializer persists LINE_STRING and METHOD entries in the cache region; every other
// object type (and every non-heap value) is filtered to Hole at serialize time, so
// 'otherSlots' covers holes and the stats-only leftovers of unimplemented cache types.
void ClassifyConstPoolCacheSlots(const JSThread* thread, const JSHandle<TaggedArray>& constpools,
                                 ConstPoolCacheStats& stats)
{
    uint32_t length = constpools->GetLength();
    for (uint32_t i = 0; i < length; i++) {
        JSTaggedValue statConstpool = constpools->Get(thread, i);
        if (!statConstpool.IsConstantPool()) {
            continue;
        }
        JSHandle<ConstantPool> statPool(thread, statConstpool);
        uint32_t numCacheSlots = statPool->GetNumOfCacheElement();
        stats.cacheSlots += numCacheSlots;
        for (uint32_t idx = 0; idx < numCacheSlots; idx++) {
            JSTaggedValue slotVal = statPool->GetObjectFromCache(thread, idx);
            if (!slotVal.IsHeapObject()) {
                stats.otherSlots++;  // hole or primitive tag
                continue;
            }
            if (slotVal.IsLineString()) {
                stats.stringSlots++;
            } else if (slotVal.IsMethod()) {
                stats.methodSlots++;
            } else {
                stats.otherSlots++;
            }
        }
    }
}

// Re-anchor deserialized Methods in this constpool's cache to this process (native pointers +
// AOT-domain defaults) before publishing. Extracted from DeserializeData to keep the function
// size in check; the loop must run before AddOrUpdateConstpool exposes the constpool.
// Patch safety: this pass rebuilds Methods from the BASE literals without consulting QuickFix.
// This holds by ordering: snapshot restore publishes the pool BEFORE any LoadPatch /
// LoadPatchIfNeeded for the same file (both registration entries require the file to be loaded
// first), and the later registration replaces these published Methods through
// PatchLoader::FindAndReplaceSameMethod exactly like fresh-filled ones. Do not move restore to
// a path that can run after patch registration without adding a CheckAndGetPatch lookup here.
// Orphan slots are counted and logged once per file by LogOrphanMethodSlots: when literals
// are missing entirely (e.g. a restore path that skipped translation), a per-slot WARN would
// flood every startup.
void ReAnchorMethodsBeforePublish(JSThread* thread, const JSPandaFile* pandafile,
                                  const JSHandle<ConstantPool>& constpoolHdl, uint32_t* orphanCount)
{
    for (uint32_t idx = 0; idx < constpoolHdl->GetNumOfCacheElement(); idx++) {
        JSTaggedValue slotVal = constpoolHdl->GetObjectFromCache(thread, idx);
        JSHandle<JSTaggedValue> slot(thread, slotVal);
        if (!slot->IsHeapObject() || !slot->IsMethod()) {
            continue;
        }
        JSHandle<Method> method = JSHandle<Method>::Cast(slot);
        EntityId eid = constpoolHdl->GetEntityId(idx);
        MethodLiteral *methodLiteral = pandafile->FindMethodLiteral(eid.GetOffset());
        if (methodLiteral == nullptr) {
            // Cannot re-anchor the serialized pointers: reset the cache slot to hole so the
            // orphaned Method is collected and ConstantPool::GetMethodFromCache rebuilds it
            // from scratch on demand. Count the slot; LogOrphanMethodSlots logs once per file.
            constpoolHdl->SetObjectToCache(thread, idx, JSTaggedValue::Hole());
            if (orphanCount != nullptr) {
                (*orphanCount)++;
            }
            continue;
        }
        // Re-initialize the Method from the MethodLiteral exactly like a fresh cache fill
        // (ObjectFactory::InitializeMethod + NewMethod's non-AOT path): the snapshot fields
        // are raw copies and may still carry old-process AOT/deopt state (IsAotCodeBit,
        // IsFastCallBit, DeoptType, IsJitCompiledCodeBit, FpDelta) next to stale pointers.
        method->SetCallField(methodLiteral->GetCallField());
        method->SetLiteralInfo(methodLiteral->GetLiteralInfo());
        method->SetNativePointerOrBytecodeArray(const_cast<void *>(methodLiteral->GetNativePointer()));
        method->SetExtraLiteralInfo(methodLiteral->GetExtraLiteralInfo());
        method->SetExpectedPropertyCount(methodLiteral->GetExpectedPropertyCount());
        method->SetCodeEntryOrLiteral(reinterpret_cast<uintptr_t>(methodLiteral));
        method->SetFpDelta(0);
        // If the literal itself carries AOT bits, downgrade to the interpreter stub entry and
        // clear the AOT flags, matching NewMethod(needSetAotFlag=false). Deliberate trade-off:
        // a snapshot written by a JIT process and restored in an AOT-loaded process gets
        // interpreter entries for these Methods (a Method cache slot does not carry the AOT
        // function entry index, so SetAOTFuncEntry cannot be re-derived here); correctness
        // stays, and AOT speed is recovered by JIT re-profiling.
        method->InitInterpreterStatusForCompiledMethod(thread);
    }
}

// Reads the snapshot data for pandafile from filePath and verifies the version header.
// On false the caller aborts: the snapshot file has already been removed on failure.
bool ReadSnapshotData(EcmaVM* vm, const JSPandaFile* pandafile, const CString& version,
                      const CString& filePath, std::unique_ptr<SerializeData>& fileData)
{
    JSThread* thread = vm->GetJSThread();
    fileData = std::make_unique<SerializeData>(thread);
    auto header = ModulesSnapshotHelper::SnapshotVersionInfo::New(vm->GetApplicationVersionCode(), version,
                                                                  pandafile->GetJSPandaFileDesc());
    if (!ModulesSnapshotHelper::ReadDataFromFile(fileData, filePath, header, "ConstPoolSnapshot")) {
        LOG_ECMA(ERROR) << "ConstPoolSnapshot::DeserializeData failed: " << filePath;
        ModulesSnapshotHelper::RemoveFile(filePath);
        return false;
    }
    return true;
}

// Initialize, re-anchor and publish one restored constpool. Returns false only on a fatal
// inconsistency (index header out of range) so the caller removes the snapshot file; a
// non-constpool entry is skipped with a WARN and counts as success.
bool PublishOneRestoredConstpool(JSThread* thread, EcmaVM* vm, const JSPandaFile* pandafile,
                                 JSTaggedValue constpoolVal, uint32_t* orphanMethodSlots)
{
    if (!constpoolVal.IsConstantPool()) {
        LOG_ECMA(WARN) << "ConstPoolSnapshot::DeserializeData: invalid constpool";
        return true;
    }
    JSHandle<ConstantPool> constpoolHdl(thread, constpoolVal);
    JSTaggedValue taggedConstpoolId = constpoolHdl->GetSharedConstpoolId();
    constpoolHdl->InitializeWithSpecialValue(thread);
    int sharedId = taggedConstpoolId.GetInt();
    auto idxHeaders = pandafile->GetPandaFile()->GetIndexHeaders();
    if (static_cast<uint32_t>(sharedId) >= idxHeaders.size()) {
        LOG_ECMA(ERROR) << "ConstPoolSnapshot::DeserializeData failed: sharedId out of range";
        return false;
    }
    const IndexHeader* idxHeader = &idxHeaders[sharedId];
    constpoolHdl->SetJSPandaFile(pandafile);
    constpoolHdl->SetIndexHeader(idxHeader);
    constpoolHdl->SetSharedConstpoolId(taggedConstpoolId);
    // Re-anchor deserialized Methods to this process before publishing the constpool
    // (see ReAnchorMethodsBeforePublish: other isolates must never observe old-process
    // addresses or compiled-state residue once AddOrUpdateConstpool exposes the pool).
    ReAnchorMethodsBeforePublish(thread, pandafile, constpoolHdl, orphanMethodSlots);
    vm->AddOrUpdateConstpool(pandafile, constpoolHdl, static_cast<int32_t>(sharedId));
    vm->CreateUnsharedConstpool(constpoolVal);
    return true;
}

// One aggregated WARN per restored file for Method cache slots that had no local
// MethodLiteral and were reset to hole.
void LogOrphanMethodSlots(const JSPandaFile* pandafile, uint32_t orphanMethodSlots)
{
    if (orphanMethodSlots == 0) {
        return;
    }
    LOG_ECMA(WARN) << "ConstPoolSnapshot::DeserializeData: " << orphanMethodSlots <<
        " orphan Method cache slot(s) reset to hole, pandafile: " <<
        pandafile->GetJSPandaFileDesc();
}

// Gated stats summary. The plain success line is the default path; the detailed line runs
// only when ENABLE_SNAPSHOT_STATS is set (one conditional branch when off).
void LogRestoreStats(const EcmaVM* vm, const JSPandaFile* pandafile, const SerializeData* fileData,
                     uint32_t constpoolCount, const ConstPoolCacheStats& stats)
{
    if (!vm->GetJSOptions().IsSnapshotStatsEnabled()) {
        LOG_ECMA(INFO) << "ConstPoolSnapshot::DeserializeData success";
        return;
    }
    LOG_ECMA(INFO) << "ConstPoolSnapshot::DeserializeData success" << ", pandafile: " <<
        pandafile->GetJSPandaFileDesc() << ", constpools: " << constpoolCount << ", cacheSlots: " <<
        stats.cacheSlots << ", strings: " << stats.stringSlots << ", methods: " <<
        stats.methodSlots << ", others: " << stats.otherSlots << ", dataSize: " << fileData->Size();
}
} // namespace

void ConstPoolSnapshot::SerializeDataAndPostSavingJob(const EcmaVM* vm, JSPandaFile* pandafile, const CString& path,
                                                      const CString& version)
{
    if (!vm->GetJSOptions().EnableConstPoolSnapshot()) {
        LOG_ECMA(DEBUG) << "ConstPoolSnapshot::SerializeDataAndPostSavingJob ConstPool Snapshot is not enabled "
            "by runtime option";
        return;
    }
    LOG_ECMA(DEBUG) << "ConstPoolSnapshot::SerializeDataAndPostSavingJob " << path;
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "ConstPoolSnapshot::SerializeDataAndPostSavingJob",
                      "");
    CString filePath = base::ConcatToCString(path, GetSnapshotFileName(pandafile));
    if (FileExist(filePath.c_str())) {
        LOG_ECMA(INFO) << "ConstPool serialize file already exist";
        return;
    }
    ModulesSnapshotHelper::MarkConstPoolSnapshotLoaded();
    JSThread* thread = vm->GetJSThread();
    std::unique_ptr<SerializeData> fileData = GetSerializeData(thread, pandafile);
    if (fileData == nullptr) {
        return;
    }
    auto header = ModulesSnapshotHelper::SnapshotVersionInfo::New(thread->GetEcmaVM()->GetApplicationVersionCode(),
                                                                  version, pandafile->GetJSPandaFileDesc());
    common::Taskpool::GetCurrentTaskpool()->PostTask(std::make_unique<SnapshotSaveTask>(
        thread->GetThreadId(), std::move(fileData), filePath, std::move(header), "ConstPoolSnapshot"));
}

bool ConstPoolSnapshot::DeserializeData(EcmaVM* vm, JSPandaFile* pandafile, const CString& path, const CString& version)
{
    if (pandafile == nullptr) {
        return false;
    }
    if (!vm->GetJSOptions().EnableConstPoolSnapshot()) {
        LOG_ECMA(DEBUG) << "ConstPoolSnapshot::DeserializeData ConstPool Snapshot is not enabled by runtime option";
        return false;
    }
    if (ModulesSnapshotHelper::IsConstPoolSnapshotDisabled(path)) {
        LOG_ECMA(DEBUG) << "ConstPoolSnapshot::DeserializeData ConstPool Snapshot is not enabled";
        ModulesSnapshotHelper::HandleCorruptedFile(path, CString(ConstPoolSnapshot::CONSTPOOL_SNAPSHOT_FILE_SUFFIX));
        return false;
    }
    ECMA_BYTRACE_NAME(HITRACE_LEVEL_COMMERCIAL, HITRACE_TAG_ARK, "ConstPoolSnapshot::DeserializeData", "");
    LOG_ECMA(DEBUG) << "ConstPoolSnapshot::DeserializeData";
    CString filePath = base::ConcatToCString(path, GetSnapshotFileName(pandafile));
    if (!FileExist(filePath.c_str())) {
        LOG_ECMA(INFO) << "ConstPoolSnapshot::DeserializeData Constpool serialize file doesn't exist: " << filePath;
        return false;
    }
    ModulesSnapshotHelper::MarkConstPoolSnapshotLoaded();
    JSThread* thread = vm->GetJSThread();
    std::unique_ptr<SerializeData> fileData;
    if (!ReadSnapshotData(vm, pandafile, version, filePath, fileData)) {
        return false;
    }
    FileDeserializer deserializer(thread, fileData.get());
    EcmaHandleScope scope(thread);
    JSHandle<TaggedArray> deserializedConstpools = JSHandle<TaggedArray>::Cast(deserializer.ReadValue());
    if (!deserializedConstpools.GetTaggedValue().IsTaggedArray()) {
        LOG_ECMA(ERROR) << "ConstPoolSnapshot::DeserializeData failed: deserializedConstpools is not tagged array";
        ModulesSnapshotHelper::RemoveFile(filePath);
        return false;
    }
    uint32_t length = deserializedConstpools->GetLength();
    ConstPoolCacheStats stats;
    if (vm->GetJSOptions().IsSnapshotStatsEnabled()) {
        ClassifyConstPoolCacheSlots(thread, deserializedConstpools, stats);
    }
    // Orphan Method slots are counted across all pools of this file and logged once
    // (per-slot WARN would flood when literals are systematically missing).
    uint32_t orphanMethodSlots = 0;
    for (uint32_t i = 0; i < length; i++) {
        JSTaggedValue constpool = deserializedConstpools->Get(thread, i);
        if (!PublishOneRestoredConstpool(thread, vm, pandafile, constpool, &orphanMethodSlots)) {
            ModulesSnapshotHelper::RemoveFile(filePath);
            return false;
        }
    }
    LogOrphanMethodSlots(pandafile, orphanMethodSlots);
    LogRestoreStats(vm, pandafile, fileData.get(), length, stats);
    return true;
}

JSHandle<JSTaggedValue> ConstPoolSnapshot::GetSerializeArray(JSThread* thread, JSPandaFile* pandafile)
{
    JSHandle<JSTaggedValue> serializedVal(thread, JSTaggedValue::Hole());
    uint32_t idx = 0;
    thread->GetEcmaVM()->ForEachSharedConstpool(
        pandafile, [thread, &serializedVal, &idx]([[maybe_unused]] int32_t index, JSTaggedValue value, size_t total) {
            if (serializedVal->IsHole()) {
                ObjectFactory* factory = thread->GetEcmaVM()->GetFactory();
                serializedVal = JSHandle<JSTaggedValue>(factory->NewTaggedArray(total));
            }
            TaggedArray::Cast(serializedVal.GetTaggedValue())->Set(thread, idx++, value);
        });
    return serializedVal;
}

std::unique_ptr<SerializeData> ConstPoolSnapshot::GetSerializeData(JSThread* thread, JSPandaFile* pandafile)
{
    FileSerializer serializer(thread);
    EcmaHandleScope scope(thread);
    JSHandle<JSTaggedValue> serializeArray = GetSerializeArray(thread, pandafile);
    if (!serializeArray->IsTaggedArray()) {
        LOG_ECMA(ERROR) << "ConstPoolSnapshot::GetSerializeData serializeArray is not tagged array";
        return nullptr;
    }
    const GlobalEnvConstants* globalConstants = thread->GlobalConstants();
    if (!serializer.WriteValue(thread, JSHandle<JSTaggedValue>(serializeArray), globalConstants->GetHandledUndefined(),
                               globalConstants->GetHandledUndefined())) {
        LOG_ECMA(ERROR) << "ConstPoolSnapshot::GetSerializeData serialize failed";
        return nullptr;
    }
    std::unique_ptr<SerializeData> fileData = serializer.Release();
    return fileData;
}

CString ConstPoolSnapshot::GetSnapshotFileName(const JSPandaFile* pandafile)
{
    return base::ConcatToCString(ModulePathHelper::GetModuleNameWithBaseFile(pandafile->GetJSPandaFileDesc()),
                                 CONSTPOOL_SNAPSHOT_FILE_SUFFIX);
}

} // namespace panda::ecmascript
