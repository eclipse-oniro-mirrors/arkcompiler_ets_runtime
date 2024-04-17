/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

#ifndef ECMASCRIPT_JIT_H
#define ECMASCRIPT_JIT_H

#include "ecmascript/common.h"
#include "ecmascript/platform/mutex.h"
#include "ecmascript/ecma_vm.h"
#include "ecmascript/mem/clock_scope.h"
#include "ecmascript/compiler/compiler_log.h"
#include "ecmascript/jit/jit_thread.h"

namespace panda::ecmascript {
class JitTask;
enum JitCompileMode {
    SYNC = 0,
    ASYNC
};
class Jit {
public:
    Jit() {}
    ~Jit();
    static PUBLIC_API Jit *GetInstance();
    void SetEnableOrDisable(const JSRuntimeOptions &options, bool isEnable);
    bool PUBLIC_API IsEnable();
    void Initialize();

    static void Compile(EcmaVM *vm, JSHandle<JSFunction> &jsFunction,
                        int32_t offset = MachineCode::INVALID_OSR_OFFSET, JitCompileMode mode = SYNC);
    bool JitCompile(void *compiler, JitTask *jitTask);
    bool JitFinalize(void *compiler, JitTask *jitTask);
    void *CreateJitCompilerTask(JitTask *jitTask);
    bool IsInitialized() const
    {
        return initialized_;
    }

    void DeleteJitCompile(void *compiler);

    void RequestInstallCode(std::shared_ptr<JitTask> jitTask);
    void InstallTasks(uint32_t threadId);
    void ClearTask(const std::function<bool(Task *task)> &checkClear);
    void ClearTask(EcmaContext *ecmaContext);
    void ClearTaskWithVm(EcmaVM *vm);
    void Destroy();
    void CheckMechineCodeSpaceMemory(JSThread *thread, int remainSize);

    // dfx for jit preheat compile
    static void CountInterpExecFuncs(JSHandle<JSFunction> &jsFunction);

    NO_COPY_SEMANTIC(Jit);
    NO_MOVE_SEMANTIC(Jit);

    class TimeScope : public ClockScope {
    public:
        explicit TimeScope(CString message, bool outPutLog = true) : message_(message), outPutLog_(outPutLog) {}
        explicit TimeScope() : message_(""), outPutLog_(false) {}
        PUBLIC_API ~TimeScope();
    private:
        CString message_;
        bool outPutLog_;
    };

    class JitLockHolder {
    public:
        explicit JitLockHolder(const CompilationEnv *env) : thread_(nullptr), scope_()
        {
            if (env->IsJitCompiler()) {
                JSThread *thread = env->GetJSThread();
                ASSERT(thread->IsJitThread());
                thread_ = static_cast<JitThread*>(thread);
                thread_->ManagedCodeBegin();
                thread_->GetHostThread()->GetJitLock()->Lock();
            }
        }

        explicit JitLockHolder(const CompilationEnv *env, CString message) : thread_(nullptr),
            scope_("Jit Compile Pass: " + message + ", Time:", false)
        {
            if (env->IsJitCompiler()) {
                JSThread *thread = env->GetJSThread();
                ASSERT(thread->IsJitThread());
                thread_ = static_cast<JitThread*>(thread);
                thread_->ManagedCodeBegin();
                thread_->GetHostThread()->GetJitLock()->Lock();
            }
        }

        ~JitLockHolder()
        {
            if (thread_ != nullptr) {
                thread_->GetHostThread()->GetJitLock()->Unlock();
                thread_->ManagedCodeEnd();
            }
        }
        JitThread *thread_ {nullptr};
        TimeScope scope_;
        ALLOW_HEAP_ACCESS
        NO_COPY_SEMANTIC(JitLockHolder);
        NO_MOVE_SEMANTIC(JitLockHolder);
    };

    class JitGCLockHolder {
    public:
        explicit JitGCLockHolder(JSThread *thread) : thread_(thread)
        {
            ASSERT(!thread->IsJitThread());
            if (Jit::GetInstance()->IsEnable()) {
                thread_->GetJitLock()->Lock();
                locked_ = true;
            }
        }

        ~JitGCLockHolder()
        {
            if (locked_) {
                thread_->GetJitLock()->Unlock();
                locked_ = false;
            }
        }

    private:
        JSThread *thread_;
        bool locked_ { false };

        NO_COPY_SEMANTIC(JitGCLockHolder);
        NO_MOVE_SEMANTIC(JitGCLockHolder);
    };

private:
    bool SupportJIT(Method *method);
    bool initialized_ { false };
    bool jitEnable_ { false };

    std::unordered_map<uint32_t, std::deque<std::shared_ptr<JitTask>>> installJitTasks_;
    Mutex installJitTasksDequeMtx_;
    Mutex setEnableLock_;
    static constexpr int MIN_CODE_SPACE_SIZE = 1_KB;

    static void (*initJitCompiler_)(JSRuntimeOptions);
    static bool(*jitCompile_)(void*, JitTask*);
    static bool(*jitFinalize_)(void*, JitTask*);
    static void*(*createJitCompilerTask_)(JitTask*);
    static void(*deleteJitCompile_)(void*);
    static void *libHandle_;
};
}  // namespace panda::ecmascript
#endif  // ECMASCRIPT_JIT_H
