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

#include "ecmascript/arksteed/arksteed_compiler.h"

#include "ecmascript/ecma_macros.h"
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ecmascript/arksteed/arksteed_assembler.h"
#include "ecmascript/arksteed/arksteed_pipeline_statistics.h"
#include "ecmascript/arksteed/arksteed_compilation_unit.h"
#include "ecmascript/arksteed/arksteed_deopt_helper.h"
#include "ecmascript/arksteed/arksteed_graph_builder.h"
#include "ecmascript/arksteed/arksteed_codegen.h"
#include "ecmascript/arksteed/arksteed_dce.h"
#include "ecmascript/arksteed/arksteed_graph_printer.h"
#include "ecmascript/arksteed/arksteed_graph_processor.h"
#include "ecmascript/arksteed/arksteed_graph_verifier.h"
#include "ecmascript/arksteed/arksteed_regalloc.h"
#include "ecmascript/arksteed/arksteed_regalloc_processors.h"
#include "ecmascript/arksteed/arksteed_write_barrier_value_kind_pass.h"
#include "ecmascript/arksteed/arksteed_safepoint_table.h"
#include "ecmascript/arksteed/arksteed_task.h"
#include "ecmascript/compiler/jit_compiler.h"
#include "ecmascript/jit/jit.h"
#include "ecmascript/jspandafile/method_literal.h"
#include "ecmascript/mem/machine_code.h"
#include "ecmascript/platform/mutex.h"

#ifdef JIT_ENABLE_CODE_SIGN
#include "ecmascript/mem/jit_fort.h"
#include "jit_buffer_integrity.h"
#endif

#include <sstream>

namespace panda::ecmascript::arksteed {

namespace {

// Parallel ArkSteed JIT workers share one compiler log stream. The graph dump
// (Starts marker → GraphPrinter → Finishes marker) is emitted line by line, so
// concurrent dumps interleave and break the runner's assumption that one dump
// block is one graph (vN numbering restarts per graph). Serialize each whole
// dump block. Only taken when print-graph is enabled; the product path stays
// lock-free.
Mutex g_graphDumpMutex;

CString ParseMethodNameFromEnv(JitCompilationEnv *env)
{
    MethodLiteral *method = env->GetMethodLiteral();
    return MethodLiteral::ParseFunctionNameToCString(env->GetJSPandaFile(), method->GetMethodId());
}

}  // namespace

static void VerifyGraph(Graph* graph, bool afterRegalloc = false)
{
#ifndef NDEBUG
    GraphProcessor<ArkSteedGraphVerifier> verifier(graph->GetChunk(), afterRegalloc);
    verifier.Run(graph);
#endif
}

static void LogAsm(ArkSteedAssembler *assembler, const CString &methodName)
{
    if (assembler == nullptr) {
        return;
    }

    // Serialize the whole codegen-ASM block like the graph dump: parallel
    // compiler workers share one log stream, so an interleaved block breaks
    // the runner's per-method attribution.
    LockHolder lock(g_graphDumpMutex);

    CommentList &comments = assembler->GetCommentList();
    uint8_t *commentsData = comments.EmitToBuffer();
    uint32_t commentsSize = comments.GetCommentsDataSize();
    std::ostringstream asmOut;
    assembler->Disassemble(asmOut, commentsData, commentsSize);
    delete[] commentsData;

    LOG_COMPILER(INFO) << "========== ArkSteed After Codegen ASM ==========";
    // The runner attributes the codegen-comment block by this line, so
    // parallel workers can emit codegen sections in any order.
    LOG_COMPILER(INFO) << "ArkSteed Codegen Method: " << methodName;
    std::istringstream asmStream(asmOut.str());
    std::string line;
    while (std::getline(asmStream, line)) {
        LOG_COMPILER(INFO) << line;
    }
    LOG_COMPILER(INFO) << "================================================";
}

ArkSteedCompilationOptions::ArkSteedCompilationOptions(JSRuntimeOptions runtimeOptions)
{
#if defined(PANDA_TARGET_AMD64)
    triple = "x86_64-unknown-linux-gnu";
#elif defined(PANDA_TARGET_ARM64)
    triple = "aarch64-unknown-linux-gnu";
#else
    LOG_JIT(FATAL) << "arksteed jit unsupport arch";
    UNREACHABLE();
#endif
    deviceIsScreenOff = runtimeOptions.GetDeviceState();
    deviceThermalLevel = runtimeOptions.GetThermalLevel();
    hotnessThreshold = runtimeOptions.GetPGOHotnessThreshold();
    profilerIn = std::string(runtimeOptions.GetPGOProfilerPath());
    enableLog = runtimeOptions.WasSetCompilerLogOption();
    printMethodName = runtimeOptions.GetCompilerArkSteedPrintMethodName();
    enableCodeComment = runtimeOptions.GetCompilerArkSteedEnableCodeComment();
}

ArkSteedCompilerTask::~ArkSteedCompilerTask()
{
    if (assembler_ != nullptr) {
        delete assembler_;
        assembler_ = nullptr;
    }
    if (safepointTableBuilder_ != nullptr) {
        delete safepointTableBuilder_;
        safepointTableBuilder_ = nullptr;
    }
    if (positionTableBuilder_ != nullptr) {
        delete positionTableBuilder_;
        positionTableBuilder_ = nullptr;
    }
    if (translationBuilder_ != nullptr) {
        delete translationBuilder_;
        translationBuilder_ = nullptr;
    }
    if (deoptLiteralTableBuilder_ != nullptr) {
        delete deoptLiteralTableBuilder_;
        deoptLiteralTableBuilder_ = nullptr;
    }
}

ArkSteedCompilerTask *ArkSteedCompilerTask::CreateJitCompilerTask(ArkSteedTask *arkSteedTask)
{
    return new (std::nothrow) ArkSteedCompilerTask(arkSteedTask);
}

void ArkSteedCompilerTask::DebugLogOnCompilationStart()
{
    if (common::Log::LogIsLoggable(Level::INFO, Component::COMPILER)) {
        LOG_COMPILER(INFO) << "======== ArkSteedCompilerTask: Starts compiling: "
                           << GetCompiledMethodName() << " ========";
    }
}

void ArkSteedCompilerTask::DebugLogOnCompilationDone()
{
    if (common::Log::LogIsLoggable(Level::INFO, Component::COMPILER)) {
        LOG_COMPILER(INFO) << "======== ArkSteedCompilerTask: Finished compiling: "
                           << GetCompiledMethodName() << " ========";
    }
}

CString ArkSteedCompilerTask::GetCompiledMethodName() const
{
    return ParseMethodNameFromEnv(jitCompilationEnv_.get());
}

bool ArkSteedCompilerTask::ShouldEnablePipelineStatistics(ArkSteedTask *arkSteedTask, JitCompilationEnv *env)
{
    const JSRuntimeOptions &options = arkSteedTask->GetHostVM()->GetJSOptions();
    if (!options.GetCompilerArkSteedPassTrace()) {
        return false;
    }
    const std::string filter = options.GetCompilerArkSteedPassTraceMethods();
    if (filter.empty()) {
        return true;
    }
    return MatchMethodName(filter, ParseMethodNameFromEnv(env));
}

bool ArkSteedCompilerTask::BuildGraph(JSThread *compilerThread)
{
    JitCompilationEnv *env = jitCompilationEnv_.get();

    // Raw heap temporaries are confined to one bytecode/read operation. The
    // builder polls between bytecodes; broker reads never park or acquire a lock.
    if (g_isEnableCMCGC) {
        return false;
    }
    JitHeapAccessScope heapAccess(compilerThread);
    DISALLOW_HEAP_ALLOC;
    topLevelUnit_ = chunk_->New<ArkSteedCompilationUnit>(env);
    BytecodePreprocessor preproc(topLevelUnit_, chunk_.get());
    {
        ArkSteedPipelineStatistics stats(pipelineStatistics_, "BytecodePreprocessor");
        if (!preproc.Run()) {
            LOG_COMPILER(WARN) << "JIT compilation halts due to bytecode preprocessing error.";
            return false;
        }
    }
    static_cast<JitThread *>(compilerThread)->HeapSafepoint();
    BytecodeAnalysis analysis(&preproc);
    {
        ArkSteedPipelineStatistics stats(pipelineStatistics_, "BytecodeAnalysis");
        if (!analysis.Run()) {
            LOG_COMPILER(WARN) << "JIT compilation halts due to bytecode analysis error.";
            return false;
        }
    }

    static_cast<JitThread *>(compilerThread)->HeapSafepoint();
    {
        ArkSteedPipelineStatistics stats(pipelineStatistics_, "GraphBuilder");
        bool isOSR = arkSteedTask_->IsOsrTask();
        GraphBuilder graphBuilder(compilerThread, graph_, &preproc, &analysis, isOSR);
        if (!graphBuilder.Run()) {
            LOG_COMPILER(WARN) << "JIT compilation halts due to graph building error.";
            return false;
        }
    }
    return true;
}

void ArkSteedCompilerTask::RunPreRegallocProcessors()
{
    // The following preprocessing passes are lightweight enough to combine their statistics data.
    ArkSteedPipelineStatistics statsScope(pipelineStatistics_, "PreRegallocProcessors");

    GraphProcessor<VertexInfoAllocateProcessor>().Run(graph_);
    GraphProcessor<ValueLocationConstraintProcessor>().Run(graph_);
    GraphProcessor<MaxCallDepthProcessor>().Run(graph_);
    GraphProcessor<LivenessProcessor>().Run(graph_);
}

bool ArkSteedCompilerTask::Compile()
{
    DebugLogOnCompilationStart();

    ArkSteedCompileTimeScope totalScope(arkSteedTask_);
    ArkSteedPipelineStatisticsSummary statsSummary(
        pipelineStatistics_, pipelineStatistics_.Enabled() ? GetCompiledMethodName() : "");

    NativeAreaAllocator *allocator = arkSteedTask_->GetCompilerVM()->GetNativeAreaAllocator();
    chunk_ = std::make_unique<Chunk>(allocator, pipelineStatistics_.Enabled());
    jitCompilationEnv_->SetHeapConstantChunk(chunk_.get());
    pipelineStatistics_.AttachChunk(chunk_.get());
    graph_ = Graph::New(chunk_.get());

    // Allocate FuncEntryDes from chunk (persists until ArkSteedCompilerTask destroyed)
    funcEntryDes_ = chunk_->New<FuncEntryDes>();

    // Debug vertex labels are scoped for the entire compilation
    VertexLabelScope vertexLabelScope;

    // Graph building phase
    auto *compilerThread = arkSteedTask_->GetCompilerThread();

    ArkSteedPGOContext pgoContext(compilerThread, jitCompilationEnv_.get());

    if (!BuildGraph(compilerThread)) {
        return false;
    }

    {
        ArkSteedPipelineStatistics stats(pipelineStatistics_, "WriteBarrierValueKindPass");
        WriteBarrierValueKindPass writeBarrierValueKindPass(graph_);
        writeBarrierValueKindPass.Run();
    }

    // Verify graph integrity before the DCE boundary
    VerifyGraph(graph_);

    // Core dead-value/Phi elimination. It runs after the existing producers
    // (graph construction, CSE/folding and `WriteBarrierValueKindPass`) and
    // before allocation metadata, location constraints, maximum call depth and
    // allocator liveness, so all four pre-regalloc consumers observe the final
    // filtered graph. The pass has no failure result: the graph is verified
    // immediately before this boundary and Release trusts internal IR;
    // unknown effects are retained by the removal policy.
    {
        ArkSteedPipelineStatistics stats(pipelineStatistics_, "CoreDce");
        ArkSteedDce::Run(graph_);
    }

    // Verify graph integrity after the DCE boundary
    VerifyGraph(graph_);
    RunPreRegallocProcessors();

    // Print graph with labeller if option is enabled
    if (arkSteedTask_->GetHostVM()->GetJSOptions().GetCompilerArkSteedPrintGraph()) {
        LockHolder lock(g_graphDumpMutex);
        LOG_COMPILER(INFO) << "===== Starts Graph Dump =====";
        // The runner attributes a dump block to a method by this line, so
        // parallel workers can dump in any order without confusing it.
        LOG_COMPILER(INFO) << "ArkSteed Graph Method: " << GetCompiledMethodName();
        bool withColors = arkSteedTask_->GetHostVM()->GetJSOptions().GetCompilerArkSteedPrintWithColors();
        GraphProcessor<GraphPrinter> graphPrinterProcessor(chunk_.get(), withColors);
        graphPrinterProcessor.Run(graph_);
        LOG_COMPILER(INFO) << "===== Finishes Graph Dump =====";
    }

    // Register allocation
    {
        ArkSteedPipelineStatistics stats(pipelineStatistics_, "RegisterAllocation");
        graph_->SetReuseStackSlots(arkSteedTask_->GetHostVM()->GetJSOptions().GetCompilerArkSteedReuseStackSlots());
        ArkSteedRegisterAllocator registerAllocator(graph_);
    }

#ifndef NDEBUG
    // RA may insert only register moves/reloads between allocation and initialization.
    // Recheck the final scheduled IR before code generation introduces machine instructions.
    VerifyGraph(graph_, true);
#endif

    // Code generation
    {
        ArkSteedPipelineStatistics stats(pipelineStatistics_, "CodeGeneration");
        assembler_ = new ArkSteedAssembler(chunk_.get(), compilerThread);
        if (arkSteedTask_->GetHostVM()->GetJSOptions().GetCompilerArkSteedEnableCodeComment()) {
            assembler_->EnableComments(true);
        }
#ifdef JIT_ENABLE_CODE_SIGN
        if (!EnableCodeSign()) {
            return false;
        }
#endif
        safepointTableBuilder_ = new ArkSteedSafepointTableBuilder(chunk_.get());
        if (arkSteedTask_->GetHostVM()->GetJSOptions().GetCompilerArkSteedSourcePositionTable()) {
            positionTableBuilder_ = new SourcePositionTableBuilder(chunk_.get());
        }
        translationBuilder_ = new DeoptTranslationBuilder();
        deoptLiteralTableBuilder_ = new DeoptLiteralTableBuilder();
        bool withColors = arkSteedTask_->GetHostVM()->GetJSOptions().GetCompilerArkSteedPrintWithColors();
        ArkSteedCodeGenerator codegen(assembler_, graph_, safepointTableBuilder_, translationBuilder_,
            deoptLiteralTableBuilder_, positionTableBuilder_, withColors, topLevelUnit_);
        codegen.Generate();

        if (arkSteedTask_->GetHostVM()->GetJSOptions().GetCompilerArkSteedPrintCode()) {
            LogAsm(assembler_, GetCompiledMethodName());
        }
    }

    DebugLogOnCompilationDone();
    statsSummary.MarkSuccess();
    return true;
}

#ifdef JIT_ENABLE_CODE_SIGN
bool ArkSteedCompilerTask::EnableCodeSign()
{
    if (Jit::GetInstance()->IsEnableJitFort() && !Jit::GetInstance()->IsDisableCodeSign() &&
        JitFort::IsResourceAvailable()) {
        OHOS::Security::CodeSign::JitCodeSigner *jitSigner = CreateJitCodeSigner();
        if (jitSigner == nullptr) {
            return false;
        }
        arkSteedTask_->SetCodeSigner(jitSigner);
        assembler_->EnableCodeSign(jitSigner);
    }
    return true;
}
#endif

namespace {
void WriteTableContainerHeader(uint8_t *buffer, size_t headerSize, size_t safepointSize, size_t positionSize,
                               size_t inlineSiteSize)
{
    bool hasInlineSites = inlineSiteSize > 0;
    auto *header = reinterpret_cast<ArkSteedTableContainerHeader *>(buffer);
    header->magic = ArkSteedTableContainerHeader::MAGIC;
    header->version = hasInlineSites ? ArkSteedTableContainerHeader::VERSION_WITH_INLINE_SITES
                                     : ArkSteedTableContainerHeader::VERSION;
    header->safepointOffset = static_cast<uint32_t>(headerSize);
    header->safepointSize = static_cast<uint32_t>(safepointSize);
    header->positionOffset = positionSize > 0 ? static_cast<uint32_t>(headerSize + safepointSize) : 0;
    header->positionSize = static_cast<uint32_t>(positionSize);
    if (hasInlineSites) {
        auto *section = reinterpret_cast<ArkSteedInlineSiteSectionHeader *>(
            buffer + ArkSteedTableContainerHeader::SIZE);
        section->inlineSiteOffset = static_cast<uint32_t>(headerSize + safepointSize + positionSize);
        section->inlineSiteSize = static_cast<uint32_t>(inlineSiteSize);
    }
}

bool CopyTableSection(uint8_t *destination, const std::unique_ptr<uint8_t[]> &source, size_t sectionSize,
                      const char *sectionName)
{
    if (sectionSize == 0) {
        return true;
    }
    if (memcpy_s(destination, sectionSize, source.get(), sectionSize) == EOK) {
        return true;
    }
    LOG_COMPILER(ERROR) << "memcpy fail in copy " << sectionName << " table to container";
    return false;
}
}  // namespace

std::unique_ptr<uint8_t[]> ArkSteedCompilerTask::BuildTableContainer(size_t *containerSize)
{
    *containerSize = 0;
    safepointTableBuilder_->SetFrameSlots(graph_->GetTaggedStackSlots(), graph_->GetUntaggedStackSlots());
    size_t safepointSize = safepointTableBuilder_->GetTableSize();
    std::unique_ptr<uint8_t[]> safepointBuffer;
    safepointBuffer.reset(safepointTableBuilder_->EmitToNewBuffer());

#ifndef NDEBUG
    LOG_COMPILER(DEBUG) << safepointTableBuilder_->DumpMemoryUsage();
#endif

    size_t positionSize = 0;
    std::unique_ptr<uint8_t[]> positionBuffer;
    if (positionTableBuilder_ != nullptr && positionTableBuilder_->GetNumEntries() > 0) {
        positionBuffer = positionTableBuilder_->EmitToNewBuffer();
        if (positionBuffer != nullptr) {
            positionSize = positionTableBuilder_->GetSize();
        }
    }

    size_t inlineSiteSize = 0;
    std::unique_ptr<uint8_t[]> inlineSiteBuffer;
    if (positionSize > 0 && positionTableBuilder_->HasInlineSites()) {
        inlineSiteBuffer = positionTableBuilder_->EmitInlineSitesToNewBuffer();
        inlineSiteSize = positionTableBuilder_->GetInlineSiteSize();
    }
    size_t headerSize = ArkSteedTableContainerHeader::SIZE;
    if (inlineSiteSize > 0) {
        headerSize += sizeof(ArkSteedInlineSiteSectionHeader);
    }
    size_t totalSize = headerSize + safepointSize + positionSize + inlineSiteSize;
    if (totalSize == 0) {
        return nullptr;
    }

    auto containerBuffer = std::make_unique<uint8_t[]>(totalSize);
    WriteTableContainerHeader(containerBuffer.get(), headerSize, safepointSize, positionSize, inlineSiteSize);
    size_t positionOffset = headerSize + safepointSize;
    size_t inlineSiteOffset = positionOffset + positionSize;
    if (!CopyTableSection(containerBuffer.get() + headerSize, safepointBuffer, safepointSize, "safepoint") ||
        !CopyTableSection(containerBuffer.get() + positionOffset, positionBuffer, positionSize, "position") ||
        !CopyTableSection(containerBuffer.get() + inlineSiteOffset, inlineSiteBuffer, inlineSiteSize, "inline site")) {
        return nullptr;
    }
    *containerSize = totalSize;
    return containerBuffer;
}

bool ArkSteedCompilerTask::FillCodeDesc(MachineCodeDesc &codeDesc)
{
    codeDesc.codeAddr = reinterpret_cast<uintptr_t>(assembler_->GetCodeBuffer());
    codeDesc.codeSize = assembler_->GetCodeSize();
    codeDesc.codeType = MachineCodeType::ARKSTEED_CODE;

    FillDeoptLiteralData();
    size_t containerSize = 0;
    auto containerBuffer = BuildTableContainer(&containerSize);
    if (containerBuffer == nullptr) {
        return false;
    }
    codeDesc.stackMapOrOffsetTableAddr = reinterpret_cast<uintptr_t>(containerBuffer.release());
    codeDesc.stackMapOrOffsetTableSize = containerSize;
#if ECMASCRIPT_ENABLE_ARK_STEED
    arkSteedTask_->SetDeoptTranslationData(translationBuilder_->Encode());
#endif
    FillEmbeddedRefData();
    FillFrameInfo();
    FillCodeComments(codeDesc);
    return true;
}

void ArkSteedCompilerTask::FillDeoptLiteralData()
{
    std::vector<JSHandle<JSTaggedValue>> deoptLiteralHandles;
    const auto &deoptLiteralHandleIndices = deoptLiteralTableBuilder_->GetHandleIndices();
    deoptLiteralHandles.reserve(deoptLiteralHandleIndices.size());
    for (uint32_t handleIndex : deoptLiteralHandleIndices) {
        CHECK(handleIndex < jitCompilationEnv_->GetHeapConstantCount());
        deoptLiteralHandles.push_back(jitCompilationEnv_->GetHeapConstantHandle(handleIndex));
    }
    CHECK(deoptLiteralHandles.size() <= std::numeric_limits<uint32_t>::max());
    safepointTableBuilder_->SetDeoptLiteralCount(static_cast<uint32_t>(deoptLiteralHandles.size()));
    arkSteedTask_->SetDeoptLiteralData(std::move(deoptLiteralHandles));
}


void ArkSteedCompilerTask::FillEmbeddedRefData()
{
    // Retain only referenced handles for installation, with indices local to this compact array.
    auto embeddedRelocations = assembler_->GetEmbeddedRefRelocations();
    std::vector<JSHandle<JSTaggedValue>> embeddedHandles;
    std::unordered_map<uint32_t, uint32_t> embeddedHandleIndices;
    for (auto &relocation : embeddedRelocations) {
        CHECK(relocation.handleIndex < jitCompilationEnv_->GetHeapConstantCount());
        auto [it, inserted] =
            embeddedHandleIndices.try_emplace(relocation.handleIndex, static_cast<uint32_t>(embeddedHandles.size()));
        if (inserted) {
            embeddedHandles.push_back(jitCompilationEnv_->GetHeapConstantHandle(relocation.handleIndex));
        }
        relocation.handleIndex = it->second;
    }
    arkSteedTask_->SetEmbeddedRefData(std::move(embeddedRelocations), std::move(embeddedHandles));
}

void ArkSteedCompilerTask::FillFrameInfo()
{
    // fpDelta = frame size + saved FP + return address
    funcEntryDes_->fpDeltaPrevFrameSp_ = 2 * static_cast<int>(sizeof(uint64_t));  // 2: saved FP + return address
    funcEntryDes_->funcSize_ = static_cast<uint32_t>(assembler_->GetCodeSize());
    funcEntryDes_->isFastCall_ = false;
    funcEntryDes_->calleeRegisterNum_ = 0;
}

void ArkSteedCompilerTask::FillCodeComments(MachineCodeDesc &codeDesc)
{
    CommentList &comments = assembler_->GetCommentList();
    if (comments.Size() > 0) {
        uint8_t *commentsData = comments.EmitToBuffer();
        codeDesc.codeCommentsAddr = reinterpret_cast<uintptr_t>(commentsData);
        codeDesc.codeCommentsSize = comments.GetCommentsDataSize();
    } else {
        codeDesc.codeCommentsAddr = 0;
        codeDesc.codeCommentsSize = 0;
    }
}

extern "C" {
PUBLIC_API void InitArkSteedCompiler(JSRuntimeOptions options)
{
    kungfu::BytecodeStubCSigns::Initialize();
    kungfu::CommonStubCSigns::Initialize();
    kungfu::BuiltinsStubCSigns::Initialize();
    kungfu::RuntimeStubCSigns::Initialize();
}

PUBLIC_API void *CreateArkSteedCompilerTask(ArkSteedTask *arkSteedTask)
{
    if (arkSteedTask == nullptr) {
        return nullptr;
    }
    return ArkSteedCompilerTask::CreateJitCompilerTask(arkSteedTask);
}

PUBLIC_API bool ArkSteedCompile(void *compilerTask, ArkSteedTask *arkSteedTask)
{
    if (arkSteedTask == nullptr || compilerTask == nullptr) {
        return false;
    }
    auto jitCompilerTask = reinterpret_cast<ArkSteedCompilerTask *>(compilerTask);
    return jitCompilerTask->Compile();
}

PUBLIC_API bool ArkSteedFinalize(void *compilerTask, ArkSteedTask *arkSteedTask)
{
    if (arkSteedTask == nullptr || compilerTask == nullptr) {
        return false;
    }
    auto task = reinterpret_cast<ArkSteedCompilerTask *>(compilerTask);

    // Fill MachineCodeDesc from compiler output
    MachineCodeDesc &machineDesc = arkSteedTask->GetMachineCodeDesc();
    if (!task->FillCodeDesc(machineDesc)) {
        return false;
    }
    if (Jit::GetInstance()->IsEnableJitFort() && Jit::GetInstance()->IsEnableAsyncCopyToFort() &&
        machineDesc.codeSize > 0) {
        if (!kungfu::JitCompiler::AllocFromFortAndCopy(*task->GetCompilationEnv(), machineDesc)) {
            return false;
        }
        machineDesc.isAsyncCompileMode = true;
    }
    arkSteedTask->SetCodeData(reinterpret_cast<const uint8_t *>(machineDesc.codeAddr), machineDesc.codeSize,
        *task->GetFuncEntryDes());
    return true;
}

PUBLIC_API void DeleteArkSteedCompilerTask(void *compilerTask)
{
    if (compilerTask == nullptr) {
        return;
    }
    delete reinterpret_cast<ArkSteedCompilerTask *>(compilerTask);
}

}  // extern "C"

}  // namespace panda::ecmascript::arksteed
