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

#include <chrono>
#include <iostream>
#include <iterator>
#include <limits>
#include <ostream>
#include <signal.h>  // NOLINTNEXTLINE(modernize-deprecated-headers)
#include <vector>

#include "ecmascript/ecma_string.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/js_runtime_options.h"
#include "ecmascript/log.cpp"
#include "ecmascript/mem/mem_controller.h"
#include "ecmascript/napi/include/jsnapi.h"
#include "libpandabase/utils/pandargs.h"
#include "libpandabase/utils/span.h"

namespace panda::ecmascript {
void BlockSignals()
{
#if defined(PANDA_TARGET_UNIX)
    sigset_t set;
    if (sigemptyset(&set) == -1) {
        LOG_ECMA(ERROR) << "sigemptyset failed";
        return;
    }
#endif  // PANDA_TARGET_UNIX
}

int Main(const int argc, const char **argv)
{
    auto startTime =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    BlockSignals();
    Span<const char *> sp(argv, argc);
    JSRuntimeOptions runtimeOptions;

    panda::PandArg<bool> help("help", false, "Print this message and exit");
    panda::PandArg<bool> options("options", false, "Print compiler and runtime options");
    // tail arguments
#if defined(PANDA_TARGET_WINDOWS)
    panda::PandArg<arg_list_t> files("files", {""}, "path to pandafiles", ";");
#else
    panda::PandArg<arg_list_t> files("files", {""}, "path to pandafiles", ":");
#endif
    panda::PandArgParser paParser;

    runtimeOptions.AddOptions(&paParser);

    paParser.Add(&help);
    paParser.Add(&options);
    paParser.PushBackTail(&files);
    paParser.EnableTail();
    paParser.EnableRemainder();

    if (!paParser.Parse(argc, argv) || files.GetValue().empty() || help.GetValue()) {
        std::cerr << paParser.GetErrorString() << std::endl;
        std::cerr << "Usage: "
                  << "panda"
                  << " [OPTIONS] [file1:file2:file3] [entrypoint] -- [arguments]" << std::endl;
        std::cerr << std::endl;
        std::cerr << "optional arguments:" << std::endl;
        std::cerr << paParser.GetHelpString() << std::endl;
        return 1;
    }

    arg_list_t arguments = paParser.GetRemainder();

    if (runtimeOptions.IsStartupTime()) {
        std::cout << "\n"
                  << "Startup start time: " << startTime << std::endl;
    }
    bool ret = true;
    EcmaVM *vm = JSNApi::CreateEcmaVM(runtimeOptions);
    if (vm == nullptr) {
        std::cerr << "Cannot Create vm" << std::endl;
        return -1;
    }

    {
        LocalScope scope(vm);
        std::string entry = runtimeOptions.GetEntryPoint();

        arg_list_t fileNames = files.GetValue();
        for (const auto &fileName : fileNames) {
            auto res = JSNApi::Execute(vm, fileName, entry);
            if (!res) {
                std::cerr << "Cannot execute panda file '" << fileName << "' with entry '" << entry << "'" << std::endl;
                ret = false;
                break;
            }
        }
    }

    JSNApi::DestroyJSVM(vm);
    paParser.DisableTail();
    return ret ? 0 : -1;
}
}  // namespace panda::ecmascript

int main(int argc, const char **argv)
{
    return panda::ecmascript::Main(argc, argv);
}
