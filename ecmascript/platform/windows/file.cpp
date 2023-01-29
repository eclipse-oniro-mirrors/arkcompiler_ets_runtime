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

#include "ecmascript/platform/file.h"

#include <climits>
#include <fileapi.h>
#include <shlwapi.h>

#ifdef ERROR
#undef ERROR
#endif

#include "ecmascript/log_wrapper.h"
#include "ecmascript/platform/map.h"

namespace panda::ecmascript {
std::string GetFileDelimiter()
{
    return ";";
}

bool RealPath(const std::string &path, std::string &realPath, [[maybe_unused]] bool readOnly)
{
    realPath = "";
    if (path.empty() || path.size() > PATH_MAX) {
        LOG_ECMA(WARN) << "File path is illeage";
        return false;
    }
    char buffer[PATH_MAX] = { '\0' };
    if (!_fullpath(buffer, path.c_str(), sizeof(buffer) - 1)) {
        LOG_ECMA(WARN) << "File path:" << path << " full path failure";
        return false;
    }
    realPath = std::string(buffer);
    return true;
}

// use CreateFile instead of _open to work with CreateFileMapping
fd_t Open(const char *file, int flag)
{
    fd_t fd = CreateFile(file, flag, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    return fd;
}

void DPrintf(fd_t fd, const std::string &buffer)
{
    LOG_ECMA(DEBUG) << "Unsupport dprintf fd(" << fd << ") in windows, buffer:" << buffer;
}

void FSync(fd_t fd)
{
    LOG_ECMA(DEBUG) << "Unsupport fsync fd(" << fd << ") in windows";
}

void Close(fd_t fd)
{
    CloseHandle(fd);
}

int64_t GetFileSizeByFd(fd_t fd)
{
    LARGE_INTEGER size;
    if (!GetFileSizeEx(fd, &size)) {
        LOG_ECMA(ERROR) << "GetFileSize failed with error code:" << GetLastError();
        return -1;
    }
    return size.QuadPart;
}

void *FileMmap(fd_t fd, uint64_t size, uint64_t offset, fd_t *extra)
{
    // 32: high 32 bits
    *extra = CreateFileMapping(fd, NULL, PAGE_PROT_READWRITE, size >> 32, size & 0xffffffff, nullptr);
    if (*extra == nullptr) {
        LOG_ECMA(ERROR) << "CreateFileMapping failed with error code:" << GetLastError();
        return nullptr;
    }
    // 32: high 32 bits
    void *addr = MapViewOfFile(*extra, FILE_MAP_ALL_ACCESS, offset >> 32, offset & 0xffffffff, size);
    if (addr == nullptr) {
        LOG_ECMA(ERROR) << "MapViewOfFile failed with error code:" << GetLastError();
        CloseHandle(*extra);
    }
    return addr;
}

int FileUnMap(void *addr, [[maybe_unused]] uint64_t size, fd_t *extra)
{
    if (UnmapViewOfFile(addr) == 0) {
        return FILE_FAILED;
    }
    if (CloseHandle(*extra) == 0) {
        return FILE_FAILED;
    }
    return FILE_SUCCESS;
}
}  // namespace panda::ecmascript
