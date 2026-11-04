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

#include <dirent.h>
#include <climits>
#include <sys/stat.h>
#include <unistd.h>
#if defined(PANDA_TARGET_LINUX) || defined(PANDA_TARGET_ANDROID) || defined(PANDA_TARGET_OHOS)
#include <sys/syscall.h>
#define PANDA_PLATFORM_HAS_RAW_SYSCALL 1
#endif
#include <functional>
#include <system_error>
#include "common_components/log/log.h"
#include "ecmascript/module/js_module_source_text.h"

namespace panda::ecmascript {
std::string GetFileDelimiter()
{
    return ":";
}

std::string GetPathSeparator()
{
    return "/";
}

bool RealPath(const std::string &path, std::string &realPath, bool readOnly)
{
    if (path.empty() || path.size() > PATH_MAX) {
        LOG_ECMA(WARN) << "File path is illegal";
        return false;
    }
    char buffer[PATH_MAX] = { '\0' };
    if (!realpath(path.c_str(), buffer)) {
        // Maybe file does not exist.
        if (!readOnly && errno == ENOENT) {
            realPath = path;
            return true;
        }
        LOG_ECMA(ERROR) << "File path: " << path << " realpath failure. errno: " << errno;
        return false;
    }
    realPath = std::string(buffer);
    return true;
}

bool RealPathByChar(const char *path, char *realPath, int rowLength, bool readOnly)
{
    if (strlen(path) == 0 || strlen(path) > PATH_MAX) {
        return false;
    }
    char buffer[PATH_MAX] = { '\0' };
    if (!realpath(path, buffer)) {
        // Maybe file does not exist.
        if (!readOnly && errno == ENOENT) {
            if (strcpy_s(realPath, rowLength, path) != 0) {
                return false;
            }
            return true;
        }
        return false;
    }
    if (strcpy_s(realPath, rowLength, buffer) != 0) {
        return false;
    }
    return true;
}

void DPrintf(fd_t fd, const std::string &buffer)
{
    int ret = dprintf(fd, "%s", buffer.c_str());
    if (ret < 0) {
        LOG_ECMA(DEBUG) << "dprintf fd(" << fd << ") failed";
    }
}

void FSync(fd_t fd)
{
    int ret = fsync(fd);
    if (ret < 0) {
        LOG_ECMA(DEBUG) << "fsync fd(" << fd << ") failed";
    }
}

void FdsanExchangeOwnerTag(fd_t fd)
{
#if defined(PANDA_TARGET_OHOS)
    fdsan_exchange_owner_tag(fd, 0, LOG_DOMAIN);
#else
    LOG_ECMA(DEBUG) << "Unsupport FdsanExchangeOwnerTag fd(" << fd << ")";
#endif
}

void Close(fd_t fd)
{
#if defined(PANDA_TARGET_OHOS)
    fdsan_close_with_tag(fd, LOG_DOMAIN);
#else
    close(fd);
#endif
}

MemMap FileMap(const char *fileName, int flag, int prot, int64_t offset)
{
    fd_t fd = open(fileName, flag);
    if (fd == INVALID_FD) {
        LOG_ECMA(ERROR) << fileName << " file open failed";
        return MemMap();
    }
    FdsanExchangeOwnerTag(fd);

    off_t size = lseek(fd, 0, SEEK_END);
    if (size <= 0) {
        Close(fd);
        LOG_ECMA(ERROR) << fileName << " file is empty";
        return MemMap();
    }

    void *addr = mmap(nullptr, size, prot, MAP_PRIVATE, fd, offset);
    Close(fd);

    if (addr == MAP_FAILED) {
        LOG_ECMA(ERROR) << fileName <<  " mmap failed, errno: " << errno
                        << ", " << strerror(errno);
        return MemMap();
    }

    return MemMap(addr, size);
}

MemMap CreateFileMap([[maybe_unused]] const char *fileName, [[maybe_unused]] int fileSize,
                     [[maybe_unused]] int flag, [[maybe_unused]] int prot)
{
    fd_t fd = open(fileName, flag, S_IRWXU | S_IRGRP | S_IROTH); // rw-r-r-
    if (fd == INVALID_FD) {
        LOG_ECMA(ERROR) << fileName << " file open failed";
        return MemMap();
    }
    FdsanExchangeOwnerTag(fd);

    if (ftruncate(fd, fileSize) == -1) {
        LOG_ECMA(ERROR) << fileName << " file ftruncate failed";
        Close(fd);
        return MemMap();
    }
    struct stat st;
    if (fstat(fd, &st) == -1 || st.st_size == 0) {
        LOG_ECMA(ERROR) << fileName << " file fstat failed";
        Close(fd);
        return MemMap();
    }

    void *addr = mmap(nullptr, fileSize, prot, MAP_SHARED, fd, 0);
    Close(fd);

    if (addr == MAP_FAILED) {
        LOG_ECMA(ERROR) << fileName <<  " mmap failed, errno: " << errno
                        << ", " << strerror(errno);
        return MemMap();
    }

    return MemMap(addr, fileSize);
}

MemMap FileMapForAlignAddressByFd(const fd_t fd, int prot, int64_t offset, uint32_t offStart)
{
    if (fd == INVALID_FD) {
        LOG_ECMA(ERROR) << fd << " fd open failed";
        return MemMap();
    }

    off_t size = lseek(fd, 0, SEEK_END);
    if (size <= 0) {
        LOG_ECMA(ERROR) << fd << " fd is empty";
        return MemMap();
    }

    void *addr = mmap(nullptr, size + offset - offStart, prot, MAP_PRIVATE, fd, offStart);

    if (addr == MAP_FAILED) {
        LOG_ECMA(ERROR) << fd <<  " fd mmap failed, errno: " << errno
                        << ", " << strerror(errno);
        return MemMap();
    }

    return MemMap(addr, size);
}

int FileUnMap(MemMap addr)
{
    return munmap(addr.GetOriginAddr(), addr.GetSize());
}

int FileSync(MemMap addr, int flag)
{
    return msync(addr.GetOriginAddr(), addr.GetSize(), flag);
}

CString ResolveFilenameFromNative(JSThread *thread, const CString &dirname,
                                  CString request)
{
    std::string relativePath;
    if (request.find("./") == 0) {
        request = request.substr(2); // 2 : delete './'
    }
    int suffixEnd = static_cast<int>(request.find_last_of('.'));
    if (request[0] == '/') { // absolute FilePath
        relativePath = request.substr(0, suffixEnd) + ".abc";
    } else {
        int pos = static_cast<int>(dirname.find_last_of('/'));
        relativePath = dirname.substr(0, pos + 1) + request.substr(0, suffixEnd) + ".abc";
    }

    std::string absPath = "";
    if (RealPath(relativePath.c_str(), absPath)) {
        CString returnValue(absPath);
        return returnValue;
    }
    THROW_REFERENCE_ERROR_AND_RETURN(thread, "resolve absolute path fail", CString());
}

bool FileExist(const char *filename)
{
    return (access(filename, F_OK) != -1);
}

int Unlink(const char *filename)
{
    return unlink(filename);
}

void *LoadLib(const std::string &libname)
{
    return dlopen(libname.c_str(), RTLD_NOW);
}

void *FindSymbol(void *handle, const char *symbol)
{
    return dlsym(handle, symbol);
}

int CloseLib(void *handle)
{
    return dlclose(handle);
}

char *LoadLibError()
{
    return dlerror();
}

void DeleteFilesWithSuffix(const std::string &dirPath, const std::string &suffix)
{
    Iterdir(dirPath, [&suffix, &dirPath](const std::string_view& fileName, FileType fileType) {
        if (fileType != FileType::REGULAR) {
            return;
        }
        if (fileName.find(suffix) == std::string::npos) {
            return;
        }
        std::string fullPath(dirPath);
        fullPath += fileName;
        if (remove(fullPath.c_str()) == 0) {
            LOG_ECMA(INFO) << "DeleteFilesWithSuffix remove path success: " << fullPath;
        } else {
            LOG_ECMA(ERROR) << "DeleteFilesWithSuffix remove path failed: " << fullPath;
        }
    });
}

std::error_code Iterdir(const std::string& path, const IterdirCallback& callback)
{
    DIR* dir = opendir(path.data());
    if (!dir) {
        LOG_FULL(ERROR) << "open dir failed, path: " << path;
        return std::error_code(errno, std::generic_category());
    }
    static const auto toFileType = [](unsigned char type) -> FileType {
        switch (type) {
            case DT_CHR:
                return FileType::CHARACTER;
            case DT_DIR:
                return FileType::DIRECTORY;
            case DT_FIFO:
                return FileType::FIFO;
            case DT_LNK:
                return FileType::SYMLINK;
            case DT_REG:
                return FileType::REGULAR;
            case DT_SOCK:
                return FileType::SOCKET;
            case DT_UNKNOWN:
            default:
                return FileType::UNKNOWN;
        }
    };
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        callback(entry->d_name, toFileType(entry->d_type));
    }
    closedir(dir);
    return std::error_code();
}

int StrToPosixFlags(const std::string_view &flags)
{
    if (flags.empty() || flags.size() > 2) { // 2: max flags length
        return -1;
    }

    if (flags.size() > 1 && flags[1] != '+') {
        return -1;
    }

    int posixFlags = 0;
    int isRW = flags.size() > 1;
    switch (flags[0]) {
        case 'r':
            posixFlags = isRW ? O_RDWR : O_RDONLY;
            break;
        case 'w':
            posixFlags = isRW ? O_RDWR : O_WRONLY;
            posixFlags |= O_CREAT | O_TRUNC;
            break;
        case 'a':
            posixFlags = isRW ? O_RDWR : O_WRONLY;
            posixFlags |= O_CREAT | O_APPEND;
            break;
        default:
            return -1;
    }

    return posixFlags;
}

constexpr mode_t StrToPosixMode(const std::string_view &mode)
{
    mode_t posixMode = 0;
    constexpr const std::string_view ALL_ACCESS = "rwxrwxrwx";
    // clang-format off
    constexpr const mode_t ACCESS_MAP[] = {
        S_IRUSR, S_IWUSR, S_IXUSR,
        S_IRGRP, S_IWGRP, S_IXGRP,
        S_IROTH, S_IWOTH, S_IXOTH,
    };
    // clang-format on

    for (size_t i = 0; i < mode.size() && i < ALL_ACCESS.size(); i++) {
        if (mode[i] == ALL_ACCESS[i]) {
            posixMode |= ACCESS_MAP[i];
        } else if (mode[i] != '-') {
            // unexpected token
            return 0;
        }
    }
    return posixMode;
}

int SeekOriginToValue(SeekOrigin whence)
{
    switch (whence) {
        case SeekOrigin::SET:
            return SEEK_SET;
        case SeekOrigin::CURRENT:
            return SEEK_CUR;
        case SeekOrigin::END:
            return SEEK_END;
        default:
            errno = EINVAL;
            return -1;
    }
}

bool Chmod(const std::string_view &path, const std::string_view &mode)
{
    // make sure path is null terminated
    std::string p(path);
    return chmod(p.c_str(), StrToPosixMode(mode)) == 0;
}

bool Chmod(const std::string_view &path, const std::string_view &mode, std::error_code &errorCode)
{
    // make sure path is null terminated
    std::string p(path);
    if (chmod(p.c_str(), StrToPosixMode(mode)) == 0) {
        errorCode.clear();
        return true;
    }
    errorCode.assign(errno, std::generic_category());
    return false;
}

std::error_code Rename(const char *oldPath, const char *newPath, bool existOk)
{
    if (!existOk && FileExist(newPath)) {
        return std::error_code(EEXIST, std::generic_category());
    }
    if (rename(oldPath, newPath) == 0) {
        return std::error_code();
    }
    return std::error_code(errno, std::generic_category());
}

namespace {
/*
 * Raw syscall wrappers used by PosixFile<true> (async-signal-safe path).
 * They bypass libc fdsan instrumentation and any libc internal state so the
 * calls can be used from a signal handler. On platforms without a usable
 * syscall() (e.g. macOS), fall back to the libc calls.
 */
inline fd_t SysOpen(const char *path, int posixFlags, mode_t mode)
{
#ifdef PANDA_PLATFORM_HAS_RAW_SYSCALL
    // arm64 has no SYS_open; openat with AT_FDCWD keeps open() semantics
    return static_cast<fd_t>(syscall(SYS_openat, AT_FDCWD, path, posixFlags, mode));
#else
    return open(path, posixFlags, mode);
#endif
}

inline ssize_t SysRead(fd_t fd, void *buf, size_t size)
{
#ifdef PANDA_PLATFORM_HAS_RAW_SYSCALL
    return syscall(SYS_read, fd, buf, size);
#else
    return read(fd, buf, size);
#endif
}

inline ssize_t SysWrite(fd_t fd, const void *data, size_t size)
{
#ifdef PANDA_PLATFORM_HAS_RAW_SYSCALL
    return syscall(SYS_write, fd, data, size);
#else
    return write(fd, data, size);
#endif
}

inline off_t SysLseek(fd_t fd, off_t off, int whence)
{
#ifdef PANDA_PLATFORM_HAS_RAW_SYSCALL
    return static_cast<off_t>(syscall(SYS_lseek, fd, off, whence));
#else
    return lseek(fd, off, whence);
#endif
}

inline int SysFstat(fd_t fd, struct stat *sa)
{
#ifdef PANDA_PLATFORM_HAS_RAW_SYSCALL
    return static_cast<int>(syscall(SYS_fstat, fd, sa));
#else
    return fstat(fd, sa);
#endif
}

inline int SysClose(fd_t fd)
{
#ifdef PANDA_PLATFORM_HAS_RAW_SYSCALL
    return static_cast<int>(syscall(SYS_close, fd));
#else
    return close(fd);
#endif
}
} // namespace

template<bool asyncSafe>
PosixFile<asyncSafe>::PosixFile(
    const std::string_view &path, const std::string_view &flags, const std::string_view &mode /* = DEFAULT_MODE */)
{
    auto m = StrToPosixMode(mode);
    if (m == 0) {
        errno = EINVAL;
        return;
    }
    auto posixFlags = StrToPosixFlags(flags);
    if constexpr (asyncSafe) {
        fd_ = SysOpen(path.data(), posixFlags, m);
    } else {
        fd_ = open(path.data(), posixFlags, m);
        if (fd_ != INVALID_FD) {
            FdsanExchangeOwnerTag(fd_);
        }
    }
}

template<bool asyncSafe>
PosixFile<asyncSafe>::~PosixFile()
{
    if (fd_ == INVALID_FD) {
        return;
    }
    if constexpr (asyncSafe) {
        SysClose(fd_);
    } else {
        Close(fd_);
    }
}

template<bool asyncSafe>
int64_t PosixFile<asyncSafe>::Write(const char *data, size_t size)
{
    if (fd_ == INVALID_FD) {
        errno = EBADF;
        return -1;
    }
    if (data == nullptr || size == 0) {
        errno = EINVAL;
        return -1;
    }
    if constexpr (asyncSafe) {
        return SysWrite(fd_, data, size);
    } else {
        return write(fd_, data, size);
    }
}

template<bool asyncSafe>
int64_t PosixFile<asyncSafe>::Read(char *buf, size_t size)
{
    if (fd_ == INVALID_FD) {
        errno = EBADF;
        return -1;
    }
    if (buf == nullptr || size == 0) {
        errno = EINVAL;
        return -1;
    }
    if constexpr (asyncSafe) {
        return SysRead(fd_, buf, size);
    } else {
        return read(fd_, buf, size);
    }
}

template<bool asyncSafe>
int64_t PosixFile<asyncSafe>::Seek(int64_t off, SeekOrigin whence)
{
    auto w = SeekOriginToValue(whence);
    if (fd_ == INVALID_FD || w == -1) {
        errno = EBADF;
        return -1;
    }
    if ((off > LONG_MAX || off < LONG_MIN) || (whence == SeekOrigin::SET && off < 0)) {
        errno = EINVAL;
        return -1;
    }
    if constexpr (asyncSafe) {
        return SysLseek(fd_, off, w);
    } else {
        return lseek(fd_, off, w);
    }
}

template<bool asyncSafe>
int64_t PosixFile<asyncSafe>::Tell()
{
    if (fd_ == INVALID_FD) {
        errno = EBADF;
        return -1;
    }
    if constexpr (asyncSafe) {
        return SysLseek(fd_, 0, SEEK_CUR);
    } else {
        return lseek(fd_, 0, SEEK_CUR);
    }
}

template<bool asyncSafe>
int64_t PosixFile<asyncSafe>::Size()
{
    if (fd_ == INVALID_FD) {
        errno = EBADF;
        return -1;
    }

    struct stat sa {};
    if constexpr (asyncSafe) {
        if (SysFstat(fd_, &sa) != 0) {
            return -1;
        }
    } else {
        if (fstat(fd_, &sa) != 0) {
            return -1;
        }
    }
    return sa.st_size;
}

template class PosixFile<true>;
template class PosixFile<false>;

} // namespace panda::ecmascript
