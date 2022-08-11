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

#ifndef ECMASCRIPT_LOG_H
#define ECMASCRIPT_LOG_H

#include <cstdint>
#include <iostream>
#include <sstream>

#include "ecmascript/common.h"
#include "generated/base_options.h"

#ifdef ENABLE_HILOG
#include "hilog/log.h"

constexpr static unsigned int DOMAIN = 0xD003F00;
constexpr static auto TAG = "ArkCompiler";
constexpr static OHOS::HiviewDFX::HiLogLabel LABEL = {LOG_CORE, DOMAIN, TAG};

#if ECMASCRIPT_ENABLE_VERBOSE_LEVEL_LOG
// print Debug level log if enable Verbose log
#define LOG_VERBOSE LOG_DEBUG
#else
#define LOG_VERBOSE LOG_LEVEL_MIN
#endif
#endif // ENABLE_HILOG

enum Level {
    VERBOSE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL,
};

namespace panda::ecmascript {
class PUBLIC_API Log {
public:
    static void Initialize(const panda::base_options::Options &options);
    static Level GetLevel()
    {
        return level_;
    }

    static void SetLevel(Level level)
    {
        level_ = level;
    }

private:
    static void SetLogLevelFromString(const std::string& level);
    static Level level_;
};

#ifdef ENABLE_HILOG
template<LogLevel level>
class HiLog {
public:
    HiLog() = default;
    ~HiLog()
    {
        if constexpr (level == LOG_LEVEL_MIN) {
            // print nothing
        } else if constexpr (level == LOG_DEBUG) {
            OHOS::HiviewDFX::HiLog::Debug(LABEL, "%{public}s", stream_.str().c_str());
        } else if constexpr (level == LOG_INFO) {
            OHOS::HiviewDFX::HiLog::Info(LABEL, "%{public}s", stream_.str().c_str());
        } else if constexpr (level == LOG_WARN) {
            OHOS::HiviewDFX::HiLog::Warn(LABEL, "%{public}s", stream_.str().c_str());
        } else if constexpr (level == LOG_ERROR) {
            OHOS::HiviewDFX::HiLog::Error(LABEL, "%{public}s", stream_.str().c_str());
        } else {
            OHOS::HiviewDFX::HiLog::Fatal(LABEL, "%{public}s", stream_.str().c_str());
            std::abort();
        }
    }
    template<class type>
    std::ostream &operator <<(type input)
    {
        stream_ << input;
        return stream_;
    }

private:
    std::ostringstream stream_;
};
#endif // ENABLE_HILOG
class StdLog {
public:
    StdLog() = default;
    ~StdLog()
    {
        std::cerr << stream_.str().c_str() << std::endl;
    }

    template<class type>
    std::ostream &operator <<(type input)
    {
        stream_ << input;
        return stream_;
    }

private:
    std::ostringstream stream_;
};
}  // namespace panda::ecmascript

#ifdef ENABLE_HILOG
#define LOG_ECMA(level) HiLogIsLoggable(DOMAIN, TAG, LOG_##level) && panda::ecmascript::HiLog<LOG_##level>()
#else
#define LOG_ECMA(level) ((level) >= panda::ecmascript::Log::GetLevel()) && panda::ecmascript::StdLog()
#endif // ENABLE_HILOG

#endif  // ECMASCRIPT_LOG_H
