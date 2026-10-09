# ArkTS AGENTS.md (ets_runtime)

## 1. 代码地图

本 AGENTS.md 适用于 `arkcompiler/ets_runtime`。
本仓库实现 ArkTS 运行时——OpenHarmony 上执行 ArkTS/TS/JS 的默认运行时。
提供字节码解释执行、AOT/JIT 编译、模块加载，并发共享，内存管理（GC）、NAPI C++ 互操作及 ES2021 标准库。

### 关键目录（按修改频率与风险分级）

| 目录 | 风险 | 原因 |
|-----------|------|-----|
| `ecmascript/arksteed/` | 🔴 高 | 轻量化JIT；平衡编译时间与优化效果 |
| `ecmascript/interpreter/` | 🔴 高 | 核心节码执行引擎；此处的缺陷会影响所有 ArkTS 应用 |
| `ecmascript/module/` | 🔴 高  | ES 模块加载器；与 CommonJS 互操作 |
| `ecmascript/mem/` | 🔴 高 | GC 与分配器；对性能和稳定性高度敏感 |
| `ecmascript/napi/` | 🟡 中 | 公共 C++ API 接口层；兼容性敏感 |
| `ecmascript/builtins/` | 🟡 中 | ES 标准库；符合ECMA2021规范 |
| `ecmascript/compiler/` | 🟡 中 | AOT 编译器；类型推断 + 代码生成，与前端紧密相关 |
| `ecmascript/jit/` | 🟡 中 | JIT 编译器；性能敏感 |
| `ecmascript/ic/` | 🟡 中 | 内联缓存；与解释器紧密耦合 |
| `ecmascript/ts_types/` | 🟡 中 | 类型元数据；影响 AOT 编译质量 |
| `ecmascript/dfx/` | 🟡 中 | 诊断/观测/性能分析（CPU 采样、堆快照、栈追踪、tracing） |
| `ecmascript/debugger/` | 🟡 中 | JS调试器（断点/单步/热重载/DropFrame） |
| `ecmascript/extractortool/` | 🟡 中 | HAP/ZIP文件提取与SourceMap解析 |
| `ecmascript/patch/` | 🟡 中 | Quick Fix/Hot Reload/Cold Patch方法替换 |
| `common_components/` | 🟢 低 | 共享基础设施；稳定，较少变更 |
| `test/` | 🟢 低 | 测试相关 |
| `docs/` | 🟢 低 | 仅文档 |

### 任务-路径快速索引

- 新增/修改字节码指令 → `ecmascript/interpreter/` + `ecmascript/ic/`
- 新增 NAPI C++ API → `ecmascript/napi/` + `test/`
- GC 或内存变更 → `ecmascript/mem/` + `common_components/heap/`
- AOT/JIT 编译器变更 → `ecmascript/compiler/` + `ecmascript/stubs/`
- 轻量化JIT编译器变更 → `ecmascript/arksteed/`
- ES 标准库变更 → `ecmascript/builtins/` + 对照 ECMA2021 规范
- 模块加载器变更 → `ecmascript/module/` + `ecmascript/require/`
- 调试器变更 → `ecmascript/debugger/` + `docs/knowledge/Debugger-Subsystem.md`
- DFX/Profiling/堆快照变更 → `ecmascript/dfx/` + `docs/knowledge/DFX-Subsystem.md`
- ExtractorTool/HAP提取/SourceMap变更 → `ecmascript/extractortool/` + `docs/knowledge/ExtractorTool-Subsystem.md`
- Quick Fix/热重载/Patch变更 → `ecmascript/patch/` + `docs/knowledge/Patch-Subsystem.md`
- 构建系统变更 → 根目录 `BUILD.gn` + `bundle.json`

## 2. 知识路由

在规划或编辑代码之前，先对任务进行分类并阅读相应的文档。
在计划中说明：任务类别、已读文档、发现的约束条件。

### 基于任务的路由

- 公共 API 或 NAPI 行为变更 → 阅读 `ecmascript/napi/AGENTS.md`
- 架构或模块边界变更 → 阅读 `docs/overview.md`
- GC、内存或分配变更 → 阅读 `docs/knowledge/ArkTS-GC.md`
- DFX、profiling、堆快照、栈追踪、tracing 变更 → 阅读 `docs/knowledge/DFX-Subsystem.md` + 子模块 AGENTS.md
- 调试、断点、dropframe、debugger hot reload 变更 → 阅读 `docs/knowledge/Debugger-Subsystem.md` + `ecmascript/debugger/AGENTS.md`
- HAP文件提取、SourceMap、ZIP解析变更 → 阅读 `docs/knowledge/ExtractorTool-Subsystem.md`
- Quick Fix、热重载、冷补丁、Patch 变更 → 阅读 `docs/knowledge/Patch-Subsystem.md`
- AOT 编译变更 → 阅读 `docs/aot-guide_zh.md` + `compiler_service/AGENTS.md`
- 模块化变更 -> 阅读 `docs/knowledge/ArkTS-Module.md`

### 基于路径的路由

- `ecmascript/napi/` → 运行时能力封装，为napi接口inner接口；变更签名前须检查兼容性
- `ecmascript/mem/` → 变更影响所有内存管理
- `ecmascript/compiler/` → AOT 代码生成；必须与 `compiler_service/` 保持同步
- `ecmascript/interpreter/` → 解释器相关修改，须进行性能回归测试
- `common_components/heap/` → 共享GC基础设施；同时影响 ets_runtime 和 runtime_core
- `ecmascript/module/` → 模块加载相关
- `ecmascript/dfx/` → 诊断/观测/性能分析子系统；先读 `docs/knowledge/DFX-Subsystem.md`
- `ecmascript/debugger/` → 调试器子系统；先读 `docs/knowledge/Debugger-Subsystem.md`
- `ecmascript/extractortool/` → 文件提取与SourceMap；先读 `docs/knowledge/ExtractorTool-Subsystem.md`
- `ecmascript/patch/` → Quick Fix/Hot Reload/Patch；先读 `docs/knowledge/Patch-Subsystem.md`
- `ecmascript/builtins/` -> ES 标准库相关

### 基于术语的路由

当任务、issue、日志、API 或变更文件涉及以下术语时，在规划前阅读对应文档：

| 术语 | 功能描述 | 阅读 |
|------|-----------|------|
| NAPI / JSNAPI | 提供napi C++ API inner接口层；签名变更会破坏下游 | `ecmascript/napi/AGENTS.md` |
| AOT / ark_aot_compiler | 机器码编译；与 compiler_service SA 协同工作 | `compiler_service/AGENTS.md` + `docs/aot-guide_zh.md` |
| GC / CMS-GC / Partial-Compressing-GC | 内存管理关键路径；修改需关注性能与稳定性风险 | `docs/knowledge/ArkTS-GC.md` |
| DFX / HiLog / heapsnapshot / CPU profiling / SIGPROF / tracing | 诊断/观测/性能分析；旁路路径不得影响主链路性能 | `docs/knowledge/DFX-Subsystem.md` |
| debugger / 调试器 / 断点 / dropframe / single step | 调试器子系统；BytecodePcChanged是热路径，非调试模式须零开销 | `docs/knowledge/Debugger-Subsystem.md` |
| interpreter / IC / 内联缓存 | 与解释器紧密耦合；性能敏感 | `ecmascript/ic/AGENTS.md` + `ecmascript/interpreter/AGENTS.md` |
| PGO | 配置文件引导优化；与 AOT 关联 | `ecmascript/` 下 PGO 子模块 AGENTS.md + `docs/aot-guide_zh.md` |
| TaskPool / Actor / Sendable| 并发模型；影响启动性能 | `docs/knowledge/ArkTS-Concurrency.md` | （待创建）
| .abc / 字节码 / jspandafile | 字节码格式；须保持向后兼容 | `ecmascript/jspandafile/AGENTS.md` |
| Stub / StubCompiler | 运行时桩代码；必须匹配解释器调用约定 | `ecmascript/stubs/AGENTS.md` |
| Module | 模块加载器 | `docs/knowledge/ArkTS-Module.md` |
| HAP / SourceMap / VLQ / Extractor / extractortool | 文件提取层，纯I/O层不得依赖JS运行时 | `docs/knowledge/ExtractorTool-Subsystem.md` |
| Quick Fix / Hot Reload / Cold Patch / patch / 热重载 / 补丁 | 运行时代码替换，方法不在调用栈上才能替换 | `docs/knowledge/Patch-Subsystem.md` |


## 3. 约束与边界

### 架构不变量

- 仅可执行由 ts2abc/es2abc 生成的 ArkCompiler 字节码（.abc 文件）。
- 仅支持 ES2021 strict 模式；不支持 sloppy 模式。
- 不支持动态代码执行（`eval`、`new Function()`）。
- NAPI 是唯一支持的 C++ 互操作路径。
- .abc 中的类型元数据须保持编译器工具链与运行时版本之间的向后兼容。
- GC 堆布局变更须保持与 runtime_core 的 ABI 兼容性。
- DFX 代码必须是旁路观测路径，采样/追踪开关关闭时须零开销或仅一次条件分支。
- Patch 方法替换必须在方法不在当前调用栈上时进行，`StageOfHotReload` 是全局状态，变更需检查所有读取点。

### 开发注意事项

- 修改 `TaggedObject` 派生类布局时，需注意不同数据类型在 32/64 位平台上的宽度差异；`JSTaggedValue` 或 GC 访问的 tagged 字段需按 `JSTaggedValue::TaggedTypeSize()` 对齐，`SIZE` 也应为该粒度的倍数，避免 `HClass` object size 截断或字段越界。
- 修改 `StringTable`/`Method`/`Module` 等虚拟机常驻基础数据结构时，须检查结构体 `SIZE` 是否增长，并提醒用户：此类变更影响进程级常驻内存（随 VM/Isolate 实例数量线性放大），需评估常驻内存与启动开销。
- 修改 `TaggedObject` 派生类的 bit 字段布局时，须用 `static_assert` 校验其与对应 `Literal` 结构字段的 offset 一致，避免类型元数据读取错位。
- 接口内引入缓存优化时，须检查缓存是否使用强引用（裸指针/`Global<>`/全局表登记）且是否设有大小上限与淘汰策略，防止常驻泄漏或无界增长。
- 使用位域或 tag pointer 时，须检查标记位是否占用指针高位（57-64 位），与 HWAsan 的 shadow 地址空间冲突。
- 修改 IR 或 C++ 解释器任一路径的字节码语义时，须检查另一路径是否存在相同逻辑并保持行为一致（见 `ecmascript/interpreter/AGENTS.md` 与 `ecmascript/compiler/AGENTS.md`）。
- 修改对象布局、删除字段或变更字段语义时，须判定是否构成不兼容变更（影响 `.abc` 类型元数据/`.so` AOT 产物/堆快照/补丁兼容），并显式提醒用户其影响范围。

### 不要做

- 除非明确要求，不要修改公共 NAPI 签名、错误码或生命周期语义。
- 未经批准，不要引入新的第三方生产依赖。
- 不要修改自动生成的文件（如 builtins stubs）；应修改源码生成器后重新生成。
- 不要移除或变更错误码而不更新 `docs/`。

## 4. 编译构建
### standalone 构建（最常用）
构建命令从OpenHarmony源码根目录执行（通常是本项目上两层路径）
```bash
# Linux
python ark.py x64.debug

# 用QEMU模拟测试ARM64用
python ark.py arm64.debug --gn-args="run_with_qemu=true"
```

### arm64测试补充说明
arm64构建不会自动生成host侧的es2abc, 跑测试前需单独构建
```bash
ninja -C out/arm64.release arm64.release/arkcompiler/ets_frontend/es2abc
```

### 快速构建方法
构建命令从OpenHarmony源码根目录执行
#### 只重编运行时so（只编译后改动的arksteed_*.cpp）
```bash
# Linux
ninja -C out/x64.debug libark_jsruntime
```
#### 跑abc/test262 需要VM可执行文件（会自动带上so依赖）
```bash
# Linux
ninja -C out/x64.debug ark_js_vm
```
#### 单元测试
```bash
# Linux
ninja -C out/x64.debug libark_jsruntime_test
```

### 完整编译命令

```bash
# 构建（在 openharmony/ 目录下执行）
../../build.sh --product-name rk3568 --build-target ark_js_host_linux_tools_packages

# 带调试符号构建
../../build.sh --product-name rk3568 --build-target ark_js_host_linux_tools_packages --gn-args is_debug=true
```

## 5. 提交与 PR 规范

向 openharmony 主仓提交代码时，commit message 与 PR 必须符合以下规范。可参考近期格式规范的 AI 辅助提交，如 `5d6dc99d2`（`fix(ic): handle JSProxy holder in LoadMiss`）。

### 5.1 Commit message 格式

```
<type>: <简短描述>                      # 第一行标题，必须 ≤48 字符（含 type 与空格）

<body>                                  # 空一行后写正文：做了什么、为什么

Issue: https://gitcode.com/openharmony/arkcompiler_ets_runtime/issues/<N>
Signed-off-by: <name> <email>

AI[100%] Human Fixed[0%] Human[0%] AI Adopted[100%]
Co-Authored-By: <tool> (<model>) <ai@local>

Change-Id: Ixxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

要点：

- **标题**：`<type>: <desc>`，**≤48 字符**。`type` 取 `fix` / `feat` / `test` / `refactor` / `docs` / `perf` 等，可带作用域如 `fix(ic):`。标题过长会被门禁拒绝。
- **正文**：空一行后说明做了什么、为什么，每行建议 ≤72 字符。
- **Issue**：**必须**先在 `https://gitcode.com/openharmony/arkcompiler_ets_runtime` 新建一个 issue（描述动机/范围/验收点），再把其完整 URL 填入 `Issue:` 行。每条 commit 都要有对应 issue。
- **Signed-off-by**：用 `git config user.name` 与 `user.email`（DCO 要求）。
- **AI 辅助标记**：由 AI 参与时加两行：
  - `AI[100%] Human Fixed[0%] Human[0%] AI Adopted[100%]`（按实际 AI 贡献比例调整百分比）
  - `Co-Authored-By: <tool> (<model>) <ai@local>`（本环境为 `opencode (glm-5.2)`；其他工具如实填写，如 `codex (gpt-5.6-sol)`）
- **Change-Id**：由 `.git/hooks/commit-msg`（repo 钩子）在首次提交时自动注入。**`git commit --amend` 时务必保留原 Change-Id**——删掉会被钩子生成新的，导致同一改动产生新评审、PR/CR 关联断裂。
- **字段顺序**：标题 →（空行）→ 正文 →（空行）→ `Issue:` → `Signed-off-by:` →（空行）→ `AI[..]` → `Co-Authored-By:` →（空行）→ `Change-Id:`。

### 5.2 推送到 fork 与创建 PR

1. **推到自己的 fork**：`git push <fork-remote> HEAD:master`，`<fork-remote>` 指向你名下的 fork（`gitcode.com/<you>/arkcompiler_ets_runtime`）。若已 amend 改写历史，用 `--force-with-lease`。
2. **创建 PR**：源 `fork:master` → 目标 `openharmony:master`。可用 gitcode API（无需网页）：
   - 取 token：`printf 'protocol=https\nhost=gitcode.com\n\n' | git credential fill` 中的 `password=` 即 personal access token（若已配置 credential helper 可直接取到缓存 token）。
   - `POST https://gitcode.com/api/v5/repos/openharmony/arkcompiler_ets_runtime/pulls`，头 `Authorization: Bearer <token>`，JSON body：`{"title","body","head":"<you>:master","base":"master"}`。返回 `number` 即 PR 号。
3. **PR 描述**：**必须用本仓模板** `.gitee/PULL_REQUEST_TEMPLATE.zh-CN.md` 填写（关联的 Issue / 修改原因 / 修改描述 / 自测试项 / 是否已执行 L0 用例）。纯测试或文档类改动，性能与 IR 自检项勾"不涉及，无需验证"；L0 用例勾"已验证"并贴构建/执行结果。可用 `PATCH .../pulls/<N>`（同上鉴权）更新 title/body；更新后若 GET 仍见旧值多为 CDN 缓存，带 `?_t=<ms>` 与 `Cache-Control: no-cache` 复查即可。

### 5.3 常用 API 速查

| 操作 | 方法 | URL |
|------|------|-----|
| 新建 issue | POST | `/api/v5/repos/openharmony/arkcompiler_ets_runtime/issues` |
| 创建 PR | POST | `/api/v5/repos/openharmony/arkcompiler_ets_runtime/pulls` |
| 查看/更新 PR | GET/PATCH | `/api/v5/repos/openharmony/arkcompiler_ets_runtime/pulls/<N>` |

鉴权统一用 `Authorization: Bearer <token>`（Gitee 风格 v5 API）。

### 5.4 端到端提交流程（AI 辅助）

以下描述 AI 辅助提交的完整端到端流程，从改动到门禁触发：

```
①  本地改动 → 编译验证（GN gen 0 ERROR + ninja target 编译通过）
②  创建 Issue（POST /issues，描述动机/范围/验收点）→ 拿到 issue number
③  git add + git commit -F commit_msg.txt（commit message 含 Issue URL + Signed-off-by + AI 标记）
④  git push <fork-remote> <branch>:refs/heads/<branch> [--force-with-lease]
⑤  创建 PR（POST /pulls，body 用 .gitee 模板，head=<fork>:<branch>, base=master）
⑥  触发门禁：POST /pulls/<N>/comments，body="start build"
    - 若 PR 含 AI 生成代码，用 "start build draft"（draft 模式，需人工确认后正式编译）
    - 若 PR 为纯人工或已人工 review，用 "start build"
⑦  轮询门禁结果：GET /pulls/<N> 看 mergeable_state / labels
    - "代码门禁通过" = 通过；"代码门禁未通过" = 需查 CI 报告修复
⑧  若门禁失败：修复 → amend（保留 Change-Id）→ force-push → 重新评论 start build
```

**AI 生成代码的 draft 流程**：

当 commit 由 AI 辅助生成时，触发编译用 `start build draft` 而非 `start build`。draft 模式下门禁执行编译但标记为草稿，需人工 review 确认后转为正式构建。流程：

```
1. AI 生成代码改动 + commit message 标记 AI[100%] / Co-Authored-By
2. push 到 fork 分支
3. 创建 PR，PR 描述自测项如实勾选
4. 评论 "start build draft" 触发 draft 编译
5. 人工 review 代码 → 确认无问题 → 评论 "start build" 转正式编译
6. 正式门禁通过后可合入
```

**门禁状态解读**：

| 标签 / 状态 | 含义 |
|---|---|
| `dco检查成功` | DCO 签名通过 |
| `静态检查成功` | 静态代码检查通过 |
| `代码门禁通过` | 编译 + 测试全通过，可合入 |
| `代码门禁未通过` | 编译或测试失败，查 CI 报告修复 |
| `waiting_on_author` | 等待作者处理 review 意见 |
| `检测到同仓同分支存在2个或以上PR` | 需先关闭旧 PR |
| `你因提交次数过多被限制提交` | 触发频率超限，需等冷却或申请恢复 |

### 5.5 Push 前强制检查清单

**每次 push 前必须逐项检查，缺一不可。不检查不 push。**

```
□ 1. Commit message 标题 ≤48 字符（含 type: 和空格）
□ 2. Commit message 含 Issue: URL（且与 PR 关联的 Issue 一致）
□ 3. Commit message 含 Signed-off-by（git config user.name / user.email）
□ 4. AI 辅助提交含 AI[...] + Co-Authored-By 两行
□ 5. Change-Id 保留（amend 时未删）
□ 6. 本地编译通过（gn gen 0 ERROR + ninja target 编译 0 FAILED）
□ 7. PR 描述使用 .gitee/PULL_REQUEST_TEMPLATE.zh-CN.md 模板
□ 8. PR 描述自测试项全部勾选（不涉及的勾"不涉及，无需验证"）
□ 9. PR 描述含测试结果（贴 GN gen / ninja 输出摘要）
□ 10. 触发编译用 "start build draft"（AI 生成代码）或 "start build"（人工确认）
```

### 5.6 示例

格式范本见本仓近期 commit `5d6dc99d2`（含 Issue / Signed-off-by / AI 行 / Co-Authored-By / Change-Id 完整字段）。
