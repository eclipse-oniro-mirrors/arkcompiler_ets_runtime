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
#include "ecmascript/compiler/pass_manager.h"
#include "ecmascript/compiler/bytecodes.h"
#include "ecmascript/compiler/compilation_driver.h"
#include "ecmascript/compiler/pass.h"
#include "ecmascript/ecma_handle_scope.h"
#include "ecmascript/jspandafile/js_pandafile_manager.h"
#include "ecmascript/jspandafile/panda_file_translator.h"
#include "ecmascript/pgo_profiler/pgo_profiler_manager.h"
#include "ecmascript/ts_types/ts_manager.h"

namespace panda::ecmascript::kungfu {
using PGOProfilerManager = pgo::PGOProfilerManager;
bool PassManager::Compile(JSPandaFile *jsPandaFile, const std::string &fileName, AOTFileGenerator &gen)
{
    [[maybe_unused]] EcmaHandleScope handleScope(vm_->GetJSThread());

    BytecodeInfoCollector collector(vm_, jsPandaFile, profilerDecoder_,
                                    maxAotMethodSize_, passOptions_->EnableCollectLiteralInfo());
    // Checking released/debuggable pandafile uses method literals, which are initialized in BytecodeInfoCollector,
    // should after it.
    if (!IsReleasedPandaFile(jsPandaFile)) {
        LOG_COMPILER(ERROR) << "The input panda file [" << fileName
                            << "] of AOT Compiler is debuggable version, do not use for performance test!";
    }

    LOptions lOptions(optLevel_, FPFlag::RESERVE_FP, relocMode_);
    CompilationDriver cmpDriver(profilerDecoder_,
                                &collector,
                                vm_->GetJSOptions().GetCompilerSelectMethods(),
                                vm_->GetJSOptions().GetCompilerSkipMethods(),
                                &gen,
                                fileName,
                                triple_,
                                &lOptions,
                                log_,
                                log_->OutputASM(),
                                maxMethodsInModule_);

    cmpDriver.Run([this, &fileName, &collector](const CString recordName,
                                                const std::string &methodName,
                                                MethodLiteral *methodLiteral,
                                                uint32_t methodOffset,
                                                const MethodPcInfo &methodPCInfo,
                                                MethodInfo &methodInfo,
                                                Module *m) {
        PassContext ctx(triple_, log_, &collector, m->GetModule(), &profilerDecoder_);
        auto jsPandaFile = ctx.GetJSPandaFile();
        auto cmpCfg = ctx.GetCompilerConfig();
        auto tsManager = ctx.GetTSManager();
        auto module = m->GetModule();
        // note: TSManager need to set current constantpool before all pass
        tsManager->SetCurConstantPool(jsPandaFile, methodOffset);
        log_->SetMethodLog(fileName, methodName, logList_);

        std::string fullName = module->GetFuncName(methodLiteral, jsPandaFile);
        bool enableMethodLog = log_->GetEnableMethodLog();
        if (enableMethodLog) {
            LOG_COMPILER(INFO) << "\033[34m" << "aot method [" << fullName
                               << "] recordName [" << recordName << "] log:" << "\033[0m";
        }
        bool hasTypes = jsPandaFile->HasTSTypes(recordName);
        if (UNLIKELY(!hasTypes)) {
            LOG_COMPILER(INFO) << "record: " << recordName << " has no types";
        }

        Circuit circuit(vm_->GetNativeAreaAllocator(), ctx.GetAOTModule()->GetDebugInfo(),
                        fullName.c_str(), cmpCfg->Is64Bit(), FrameType::OPTIMIZED_JS_FUNCTION_FRAME);

        PGOProfilerDecoder *decoder = passOptions_->EnableOptPGOType() ? &profilerDecoder_ : nullptr;

        BytecodeCircuitBuilder builder(jsPandaFile, methodLiteral, methodPCInfo, tsManager, &circuit,
                                       ctx.GetByteCodes(), hasTypes, enableMethodLog && log_->OutputCIR(),
                                       passOptions_->EnableTypeLowering(), fullName, recordName, decoder, false,
                                       passOptions_->EnableOptTrackField());
        {
            TimeScope timeScope("BytecodeToCircuit", methodName, methodOffset, log_);
            builder.BytecodeToCircuit();
        }

        PassData data(&builder, &circuit, &ctx, log_, fullName, &methodInfo, hasTypes, recordName,
                      methodLiteral, methodOffset, vm_->GetNativeAreaAllocator(), decoder, passOptions_);
        PassRunner<PassData> pipeline(&data);
        pipeline.RunPass<RunFlowCyclesVerifierPass>();
        pipeline.RunPass<RedundantPhiEliminationPass>();
        if (builder.EnableLoopOptimization()) {
            pipeline.RunPass<LoopOptimizationPass>();
            pipeline.RunPass<RedundantPhiEliminationPass>();
        }
        pipeline.RunPass<TypeInferPass>();
        if (data.IsTypeAbort()) {
            data.AbortCompilation();
            return;
        }
        pipeline.RunPass<PGOTypeInferPass>();
        pipeline.RunPass<TSClassAnalysisPass>();
        pipeline.RunPass<TSInlineLoweringPass>();
        pipeline.RunPass<RedundantPhiEliminationPass>();
        pipeline.RunPass<AsyncFunctionLoweringPass>();
        // skip async function, because some application run with errors.
        if (methodInfo.IsTypeInferAbort()) {
            data.AbortCompilation();
            return;
        }
        pipeline.RunPass<TSHCRLoweringPass>();
        pipeline.RunPass<RedundantPhiEliminationPass>();
        pipeline.RunPass<NTypeHCRLoweringPass>();
        if (data.IsTypeAbort()) {
            data.AbortCompilation();
            return;
        }
        pipeline.RunPass<EarlyEliminationPass>();
        pipeline.RunPass<ArrayBoundsCheckEliminationPass>();
        pipeline.RunPass<NumberSpeculativePass>();
        pipeline.RunPass<LaterEliminationPass>();
        pipeline.RunPass<ValueNumberingPass>();
        pipeline.RunPass<StateSplitLinearizerPass>();
        pipeline.RunPass<NTypeMCRLoweringPass>();
        pipeline.RunPass<TypeMCRLoweringPass>();
        pipeline.RunPass<LaterEliminationPass>();
        pipeline.RunPass<EarlyEliminationPass>();
        pipeline.RunPass<LCRLoweringPass>();
        pipeline.RunPass<ConstantFoldingPass>();
        pipeline.RunPass<ValueNumberingPass>();
        pipeline.RunPass<SlowPathLoweringPass>();
        pipeline.RunPass<ValueNumberingPass>();
        pipeline.RunPass<VerifierPass>();
        pipeline.RunPass<GraphLinearizerPass>();
        pipeline.RunPass<LLVMIRGenPass>();
    });

    ProcessConstantPool(&collector);
    return true;
}

void PassManager::ProcessConstantPool(BytecodeInfoCollector *collector)
{
    LOG_COMPILER(INFO) << collector->GetBytecodeInfo().GetSkippedMethodSize()
                       << " methods have been skipped";
    vm_->GetJSThread()->GetCurrentEcmaContext()->GetTSManager()->ProcessSnapshotConstantPool(collector);
}

bool PassManager::IsReleasedPandaFile(const JSPandaFile *jsPandaFile) const
{
    MethodLiteral* methodLiteral = jsPandaFile->GetMethodLiterals();
    if (methodLiteral == nullptr) {
        LOG_COMPILER(ERROR) << "There is no mehtod literal in " << jsPandaFile->GetJSPandaFileDesc();
        return false;
    }

    panda_file::File::EntityId methodId = methodLiteral->GetMethodId();
    ASSERT(methodId.IsValid());
    DebugInfoExtractor *debugInfoExtractor = JSPandaFileManager::GetInstance()->GetJSPtExtractor(jsPandaFile);
    LocalVariableTable lvt = debugInfoExtractor->GetLocalVariableTable(methodId);
    return lvt.empty();
}
} // namespace panda::ecmascript::kungfu
