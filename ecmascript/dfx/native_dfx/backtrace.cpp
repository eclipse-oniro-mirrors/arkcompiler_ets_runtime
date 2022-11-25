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

#include "ecmascript/dfx/native_dfx/backtrace.h"

#include <dlfcn.h>
#include <iomanip>
#include <ios>
#include <cstring>
#include <unwind.h>

#include "ecmascript/log_wrapper.h"
#include "mem/mem.h"

namespace panda::ecmascript {
static const std::string LIB_UNWIND_SO_NAME = "libunwind.so";
static const std::string LIB_UNWIND_Z_SO_NAME = "libunwind.z.so";
static const int MAX_STACK_SIZE = 16;
static const int ALIGN_WIDTH = 2;

using UnwBackTraceFunc = int (*)(void**, int);

void PrintBacktrace(uintptr_t value)
{
    static UnwBackTraceFunc unwBackTrace = nullptr;
    if (!unwBackTrace) {
        void *handle = dlopen(LIB_UNWIND_SO_NAME.c_str(), RTLD_NOW);
        if (handle == nullptr) {
            handle = dlopen(LIB_UNWIND_Z_SO_NAME.c_str(), RTLD_NOW);
            if (handle == nullptr) {
                LOG_ECMA(ERROR) << "dlopen libunwind.so failed";
                return;
            }
        }
        unwBackTrace = reinterpret_cast<UnwBackTraceFunc>(dlsym(handle, "unw_backtrace"));
        if (unwBackTrace == nullptr) {
            LOG_ECMA(ERROR) << "dlsym unw_backtrace failed";
            return;
        }
    }

    void *buffer[MAX_STACK_SIZE] = { nullptr };
    int level = unwBackTrace((void**)&buffer, MAX_STACK_SIZE);
    std::ostringstream stack;
    for (int i = 1; i < level; i++) {
        const char *file = "";
        uintptr_t offset = 0;
        Dl_info info;
        if (dladdr(buffer[i], &info)) {
            if (info.dli_fname) {
                file = info.dli_fname;
            }
            if (info.dli_fbase) {
                offset = ToUintPtr(buffer[i]) - ToUintPtr(info.dli_fbase);
            }
        }
        stack << "#" << std::setw(ALIGN_WIDTH) << std::dec << i << ":  "
              << file << "(" << "+" << std::hex << offset << ")" << std::endl;
    }
    LOG_ECMA(INFO) << "=====================Backtrace(" << std::hex << value <<")========================";
    LOG_ECMA(INFO) << stack.str();
    stack.clear();
}
} // namespace panda::ecmascript
