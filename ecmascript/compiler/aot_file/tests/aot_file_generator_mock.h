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

#ifndef ECMASCRIPT_COMPILER_AOT_FILE_TESTS_AOT_FILE_GENERATOR_MOCK_H
#define ECMASCRIPT_COMPILER_AOT_FILE_TESTS_AOT_FILE_GENERATOR_MOCK_H

#include <unordered_map>
#include "ecmascript/compiler/aot_compilation_env.h"
#include "ecmascript/compiler/aot_file/elf_builder.h"
#include "ecmascript/compiler/file_generators.h"
#include "ecmascript/mem/c_string.h"

namespace panda::ecmascript::kungfu {
class AOTFileGeneratorMock : public AOTFileGenerator {
public:
    AOTFileGeneratorMock(CompilerLog *log, AotMethodLogList *logList, AOTCompilationEnv *env, const std::string &triple,
                         bool isEnableLiteCG, size_t anFileMaxByteSize)
        : AOTFileGenerator(log, logList, env, triple, isEnableLiteCG, anFileMaxByteSize)
    {
    }

public:
    bool SaveAndGetAOTFileSize(const std::string &filename, const std::string &appSignature, size_t &anFileSize,
                               std::unordered_map<CString, uint32_t> &fileNameToChecksumMap)
    {
        bool ret = SaveAOTFile(filename, appSignature, fileNameToChecksumMap);
        ElfBuilder testBuilder(aotInfo_.GetModuleSectionDes(), aotInfo_.GetDumpSectionNames(), false,
                               Triple::TRIPLE_AMD64);
        anFileSize = testBuilder.CalculateTotalFileSize();
        return ret;
    }
};
}  // namespace panda::ecmascript::kungfu

#endif  // ECMASCRIPT_COMPILER_AOT_FILE_TESTS_AOT_FILE_GENERATOR_MOCK_H