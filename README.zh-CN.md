# mcpp-ide - mcpp 的 IDE 层

> 所有编辑器共用一个后端来对待 [mcpp](https://github.com/mcpp-community/mcpp) 工程：VS Code（mcpp-vscode）、
> CLion（mcpp-clion）与 Neovim（mcpp-nvim）的构建、运行、测试、调试、打包行为一致

[English](README.md) | **简体中文**

[文档](docs/README.md) · [协议](protocol/protocol.schema.json) · [设置](spec/settings.json) ·
[面向 Agent](agents/README.md) · [发布](https://github.com/mcpp-community/mcpp-ide/releases)

构建工具的编辑器集成要做许多决定：构建运行哪条命令、测试结果是什么含义、一次清理有多危险、工程为哪个目标构建。
每个编辑器各写一遍，这些决定就会分歧；从一个编辑器换到另一个，用户遇到的就是另一个工具。

mcpp-ide 只做一次。它的后端 `mcpp-ide serve` 是一个 C++ 程序，只读取 mcpp 的机器契约（事件流、结果记录、启动描述、
JSON 查询），以 LSP 服务器（JSON-RPC，协议 2）回答编辑器。VS Code 扩展的视图与页面即 mcpp-vscode 0.9.0 的视图与页面；
CLion 插件以 CLion 自己的树、列表与设置表单显示同样的信息（图在 JCEF 中）；Neovim 插件把这些回答映射到缓冲区、浮动窗口与
quickfix 列表。各编辑器插件位于本仓库的 `mide-plugins/`。C++ 文件的语言功能来自
[mcppls](https://github.com/Sunrisepeak/mcpp-language-server)；mcpp.toml 与 build.mcpp 的语言功能来自后端
（[docs/rules.md](docs/rules.md)）。

## 功能

| | VS Code（mcpp-vscode） | CLion（mcpp-clion） | Neovim（mcpp-nvim） |
|---|---|---|---|
| 构建、运行、测试 | 伪终端中的任务，显示 mcpp 自身的输出 | "mcpp" 运行配置、PTY 控制台、`main` 旁的 ▶ | `:Mcpp build`、`test`：任务输出流入缓冲区；`:Mcpp run` 在终端缓冲区中运行 |
| 测试树、单个测试重跑 | 测试视图：程序、文件、用例；`@kind:bench` 与用例的标签可筛选 | IDE 测试运行器：每个程序、每个文件一个套件 | `:Mcpp test <filter>`：`mcpp://tests`、标记、quickfix 列表 |
| 测试结果 | 逐用例实时显示；陈述两侧的失败可打开 VS Code 的差异视图；用例所在行末显示耗时与指标 | 逐用例实时显示；比较型失败可打开 IDE 的差异视图；指标在用例输出中 | 逐用例实时显示；两侧写入缓冲区与 quickfix 列表；耗时与指标以虚拟文本显示 |
| 测试洞察（mcpp 保留的运行） | "mcpp: Show Test Insights"，一个 webview | mcpp 工具窗口的 Test Insights 页（JCEF） | `:Mcpp test.insights`，浮动窗口 |
| 调试 | CodeLLDB、C/C++（gdb、lldb）、lldb-dap | CLion 的 GDB 或 LLDB | nvim-dap（codelldb、lldb-dap、cppdbg、gdb） |
| 诊断 | 问题面板，来自 mcpp 带位置的诊断 | 控制台链接到报告的位置 | `vim.diagnostic`，`:Mcpp problems` 填入 quickfix 列表 |
| mcpp.toml、build.mcpp | 补全、悬停、跳转与规则诊断来自后端（语言客户端）；问题来自 mcpp | 同上，经 CLion 的补全、文档、标注与跳转 | 同上，经 `vim.lsp`；mcpp 的问题在 `vim.diagnostic` |
| 库、包页面、依赖图、缓存、设置 | mcpp-vscode 的视图与页面 | 原生的树与列表、由设置注册表生成的设置表单、JCEF 中的图 | 浮动窗口中的文本、`vim.ui.select`；设置在 `setup()` 中 |
| 目标（含目标集合）、打包 | 快速选择 | 弹出列表 | `vim.ui.select` |
| 语言功能 | mcppls（随扩展一并安装） | mcppls CLion 插件（依赖） | mcppls-nvim（`xlings install mcppls-nvim`） |

CLion 的 **Settings | Tools | mcpp** 在保存表单前检查数值是否有限、是否处于允许范围内。
无效输入会显示错误，并保持所有已存储的值不变。输入某项设置的默认值会移除其存储覆盖。

图查询失败时显示构建数据库错误。修正所报告的问题后，重新打开图即可重试。

**测试。** 编辑器按 `mcpp test` 的陈述显示测试（[docs/architecture.md](docs/architecture.md)，
"A test run, end to end"）。多文件程序（`[test] programs = "directory"`）以其文件为子项，每个文件是一次运行中
单独成进程的那一部分。mtest 测试协议的用例带有其类别、标签与指标上下限。测试洞察读取 mcpp 保存在
`target/mtest` 的运行：最近一次运行、其失败及起始时间、各指标与上下限、最慢与不稳定的用例、调度与关键路径。
编辑器所显示的内容取决于工程的 mcpp：

| 工程的 mcpp | 增加 |
|---|---|
| 任意版本 | 程序及其结果 |
| 2026.11.8.1（`test-protocol`） | 用例、其位置与结果、失败的两侧 |
| 2026.11.9.1 | 多文件程序的文件；基于已保留运行的测试洞察 |
| `test-parts`、`test-case-stream`、`test-metrics`（[mcpp#917](https://github.com/mcpp-community/mcpp/issues/917)） | 由 mcpp 陈述的部分、逐用例结束时的结果、指标及其趋势 |

工程的 mcpp 未声明某视图所需的能力时，该视图不显示；自检（`capabilities/explain`）说明哪个版本的 mcpp 声明了它。

- **处处同一答案**——构建运行什么、测试如何报告、清理的代价：都由后端决定
- **只读 mcpp 的契约**——不自行解析清单，不自行解析依赖
- **任务有序**——每个工程同一时间一个任务，新构建替换等待中的构建，取消会正常结束
- **平台可见**——每个目标连同配置它的平台，以及每个值的来源
- **安全清理**——带危险等级与预览的计划，按该等级要求确认

## 安装

### 让 Agent 安装

```
阅读 https://github.com/mcpp-community/mcpp-ide 的 agents/README.md，为我使用的编辑器（VS Code、CLion 或 Neovim）
从最新发布安装 mcpp-ide，并在我的 mcpp 工程上用 `mcpp-ide self-check` 检查。
```

### 从 release 安装

| 编辑器 | 资产 | 安装 |
|---|---|---|
| VS Code | Marketplace（`mcpp-community.mcpp-vscode`），或 `mcpp-vscode-<version>.vsix` | 搜索 *mcpp*，或 `code --install-extension mcpp-vscode-<version>.vsix` |
| 兼容 VS Code 的编辑器 | Open VSX，或 `.vsix` | 在扩展视图中安装 `.vsix` |
| CLion 2026.1+ | JetBrains Marketplace（`mcpp-clion`），或 `mcpp-clion-<version>.zip` | Settings → Plugins → Install Plugin from Disk；先安装 mcppls CLion 插件（本系统的包自带服务端） |
| Neovim 0.10+ | release 源码树中的 `mide-plugins/nvim` | `xlings install mcpp-ide-nvim` 与 `xlings install mcpp-ide`，再 `require('mcpp').setup()`；或把该目录加入 runtimepath（[其 README](mide-plugins/nvim/README.md)） |
| Agent、其他客户端 | `mcpp-ide-<version>-openkal-bundle-all.tar.gz`（两个架构的映像；Linux x64/arm64、macOS arm64、Windows x64 的 okrun），携带 okrun 的 `mcpp-ide-<version>.okb`，或 `xlings install mcpp-ide` | 运行 `bin/mcpp-ide serve`（Windows 上为 `bin\mcpp-ide.exe`），或 `okrun mcpp-ide-<version>.okb serve` |

每个编辑器包都服务全部系统：后端以一个 ok 映像包（`backend/mcpp-ide.okb`，openkal-platform 0.24 上的
x86_64 与 aarch64 映像）加上 Linux x64/arm64、macOS arm64、Windows x64 的 okrun 携带。Neovim 插件不携带后端：
它启动 xlings 安装的 `mcpp-ide` 命令（由 okrun 运行同一个映像包）。
VS Code 和 CLion 仅在用户主动执行依赖本地构建工具的操作时检查 `mcpp`；未安装时只提示一次，并提供
[mcpp 开源仓库](https://github.com/mcpp-community/mcpp)链接。

**版本。** 版本为 `X.Y.Z`。`X.Y` 是系列，由后端与全部插件共享：插件与同系列的任何后端配合，遇到其他系列时拒绝，
并写出双方的版本与对应的安装方法。VS Code 与 CLion 的包内含后端，因此后端的改动即是全部包的改动：它开启一个新系列，
全部组件为 `X.Y.0`，标签为 `vX.Y.0`；后端始终是所属系列的 `X.Y.0`。`Z` 是编辑器插件自己的补丁，标签为 `vscode-`、
`clion-` 或 `nvim-vX.Y.Z`，只发布该插件，所带的后端仍是本系列的那一份：VS Code 1.3.2 与 CLion 1.3.1 都带后端 1.3.0。
自本系列的标签以来后端的任何路径有改动时，插件补丁被拒绝。
拉取请求只运行它所改动的组件的作业（[`.github/components.tsv`](.github/components.tsv)）：后端的改动运行全部作业；
一个插件的改动只运行该插件的作业，所用的后端是 main 以相同输入构建的那一份；文档的改动只运行文档的检查；`.github/`
与表中没有列出的路径运行全部作业。唯一必需的检查是 `ci`。

### 从源码构建

```bash
mcpp build                                     # 后端、mock mcpp、mcpp-ide-dev
mcpp test                                      # 后端测试（mtest：tests/unit、e2e、cts、bench）
cd mide-plugins/vscode && npm ci && npm test   # 扩展的测试，含对 0.9.0 的一致性门
npm run test:webui                             # 图页面与测试页面在 Chromium 中、经后端运行
npm run package                                # dist/mcpp-vscode-<version>.vsix
cd ../clion && gradle buildPlugin verifyPlugin # build/distributions/mcpp-clion-<version>.zip
cd ../.. && nvim --headless --clean -l mide-plugins/nvim/tests/smoke.lua   # Neovim 插件的测试
```

`xlings install gradle` 会一并带来 Gradle 及其 JDK。

## 文档

- [docs/architecture.md](docs/architecture.md)——三层及其边界
- [protocol/protocol.schema.json](protocol/protocol.schema.json)——后端的方法与通知
- [protocol/mcpp-contract.schema.json](protocol/mcpp-contract.schema.json)——后端读取的 mcpp 契约部分
- [spec/](spec)——设置、命令、翻译：各编辑器声明与命令表的唯一来源
- [mide-plugins/vscode/README.md](mide-plugins/vscode/README.md)、[mide-plugins/clion/README.md](mide-plugins/clion/README.md)、
  [mide-plugins/nvim/README.md](mide-plugins/nvim/README.md)

## 参与贡献

后端是用 mcpp 构建的 C++23 模块程序；仓库的工具是它的一个程序（`mcpp-ide-dev`：抓取 fixture、生成编辑器声明、
检查翻译）。生成物不提交。mcpp 的输出有变化时，从 `mcpp-ide-dev capture` 与 `protocol/fixtures` 的差异开始。

## 许可证

Apache-2.0
