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

#ifndef ECMASCRIPT_JSPANDAFILE_JS_PANDAFILE_MANAGER_H
#define ECMASCRIPT_JSPANDAFILE_JS_PANDAFILE_MANAGER_H

#include "ecmascript/mem/c_containers.h"
#include "ecmascript/class_linker/panda_file_translator.h"
#include "ecmascript/jspandafile/js_pandafile.h"
#include "ecmascript/tooling/backend/js_pt_extractor.h"
#include "libpandafile/file.h"
#include "libpandabase/utils/logger.h"

namespace panda {
namespace panda_file {
class File;
class EcmaVm;
}  // namespace panda_file

namespace ecmascript {
class Program;

class PUBLIC_API JSPandaFileManager {
public:
    static JSPandaFileManager *GetInstance()
    {
        static JSPandaFileManager jsFileManager;
        return &jsFileManager;
    }

    JSPandaFileManager() = default;
    ~JSPandaFileManager();

    JSHandle<Program> GenerateProgram(EcmaVM *vm, const JSPandaFile *jsPandaFile);

    const JSPandaFile *LoadPfAbc(const std::string &filename);

    const JSPandaFile *LoadBufferAbc(const std::string &filename, const void *buffer, size_t size);

    const JSPandaFile *GetJSPandaFile(const panda_file::File *pf);

    const JSPandaFile *CreateJSPandaFile(const panda_file::File *pf, const CString &desc);

    tooling::JSPtExtractor *GetJSPtExtractor(const JSPandaFile *pf);

    static void RemoveJSPandaFile(void *pointer, void *data);

    // for debugger
    template<typename Callback>
    void EnumerateJSPandaFiles(Callback cb)
    {
        // since there is a lock, so cannot mark function const
        os::memory::LockHolder lock(jsPandaFileLock_);
        for (const auto &iter : loadedJSPandaFiles_) {
            if (!cb(iter.first)) {
                return;
            }
        }
    }

private:
    class JSPandaFileAllocator {
    public:
        static void *AllocateBuffer(size_t size);
        static void FreeBuffer(void *mem);
    };

    void ReleaseJSPandaFile(const JSPandaFile *jsPandaFile);
    const JSPandaFile *FindJSPandaFileUnlocked(const CString &filename);
    void InsertJSPandaFile(const JSPandaFile *jsPandaFile);
    void IncreaseRefJSPandaFileUnlocked(const JSPandaFile *jsPandaFile);
    void DecreaseRefJSPandaFile(const JSPandaFile *jsPandaFile);

    static void *AllocateBuffer(size_t size);
    static void FreeBuffer(void *mem);

    os::memory::RecursiveMutex jsPandaFileLock_;
    std::unordered_map<const JSPandaFile *, uint32_t> loadedJSPandaFiles_;

    friend class JSPandaFile;
};
}  // namespace ecmascript
}  // namespace panda
#endif // ECMASCRIPT_JSPANDAFILE_JS_PANDAFILE_MANAGER_H
