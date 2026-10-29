# mcpp-ide - mcpp 的 IDE 层

> 所有编辑器共用一个后端来对待 [mcpp](https://github.com/mcpp-community/mcpp) 工程：VS Code（mcpp-vscode）
> 与 CLion（mcpp-clion）的构建、运行、测试、调试、打包行为一致

[English](README.md) | **简体中文**

[文档](docs/README.md) · [协议](protocol/protocol.schema.json) · [设置](spec/settings.json) ·
[面向 Agent](agents/README.md) · [发布](https://github.com/mcpp-community/mcpp-ide/releases)

构建工具的编辑器集成要做许多决定：构建运行哪条命令、测试结果是什么含义、一次清理有多危险、工程为哪个目标构建。
每个编辑器各写一遍，这些决定就会分歧；从一个编辑器换到另一个，用户遇到的就是另一个工具。

mcpp-ide 只做一次。它的后端 `mcpp-ide serve` 是一个 C++ 程序，只读取 mcpp 的机器契约（事件流、结果记录、启动描述、
JSON 查询），通过 JSON-RPC 回答编辑器。VS Code 扩展与 CLion 插件把这些回答映射到各自的界面，并共用一套网页面板。
C++ 文件的语言功能来自 [mcppls](https://github.com/Sunrisepeak/mcpp-language-server)；mcpp-ide 不注册任何语言服务。

## 功能

| | VS Code（mcpp-vscode） | CLion（mcpp-clion） |
|---|---|---|
| 构建、运行、测试 | 伪终端中的任务，显示 mcpp 自身的输出 | "mcpp" 运行配置、PTY 控制台、`main` 旁的 ▶ |
| 测试树、单个测试重跑 | 测试视图 | IDE 测试运行器 |
| 调试 | CodeLLDB、C/C++（gdb、lldb）、lldb-dap | CLion 的 GDB 或 LLDB |
| 诊断 | 问题面板，来自 mcpp 带位置的诊断 | 控制台链接到报告的位置 |
| mcpp.toml | 问题来自 mcpp；补全、悬停、大纲来自 mcpp 的键注册表 | 问题来自 mcpp |
| 目标与平台、库、依赖图、缓存、打包、设置 | 共享网页面板 | 同一套面板（JCEF） |
| 语言功能 | mcppls（随扩展一并安装） | mcppls CLion 插件（依赖） |

- **处处同一答案**——构建运行什么、测试如何报告、清理的代价：都由后端决定
- **只读 mcpp 的契约**——不自行解析清单，不自行解析依赖
- **任务有序**——每个工程同一时间一个任务，新构建替换等待中的构建，取消会正常结束
- **平台可见**——每个目标连同配置它的平台，以及每个值的来源
- **安全清理**——带危险等级与预览的计划，按该等级要求确认

## 安装

### 让 Agent 安装

```
阅读 https://github.com/mcpp-community/mcpp-ide 的 agents/README.md，为我使用的编辑器（VS Code 或 CLion）
从最新发布安装 mcpp-ide，并在我的 mcpp 工程上用 `mcpp-ide self-check` 检查。
```

### 从 release 安装

| 编辑器 | 资产 | 安装 |
|---|---|---|
| VS Code | Marketplace（`mcpp-community.mcpp-vscode`），或 `mcpp-vscode-<version>.vsix` | 搜索 *mcpp*，或 `code --install-extension mcpp-vscode-<version>.vsix` |
| 兼容 VS Code 的编辑器 | Open VSX，或 `.vsix` | 在扩展视图中安装 `.vsix` |
| CLion 2026.1+ | JetBrains Marketplace（`mcpp-clion`），或 `mcpp-clion-<version>.zip` | Settings → Plugins → Install Plugin from Disk；先安装 mcppls CLion 插件（本系统的包自带服务端） |
| Agent、其他客户端 | `mcpp-ide-<version>-openkal.tar.gz`（两个架构的映像；Linux x64/arm64、macOS arm64、Windows x64 的 okrun），携带 okrun 的 `mcpp-ide-<version>.okb`，或 `xlings install mcpp-ide` | 运行 `bin/mcpp-ide serve`（Windows 上为 `bin\mcpp-ide.cmd`），或 `okrun mcpp-ide-<version>.okb serve` |

每个编辑器包都服务全部系统：后端以一个 ok 映像包（`backend/mcpp-ide.okb`，openkal-platform 0.17.3 上的
x86_64 与 aarch64 映像）加上 Linux x64/arm64、macOS arm64、Windows x64 的 okrun 携带。

### 从源码构建

```bash
mcpp build                        # 后端、mock mcpp、mcpp-ide-dev
mcpp test                         # 后端测试
cd vscode && npm ci && npm test   # 扩展与面板的测试
npm run package                   # dist/mcpp-vscode-<version>.vsix
cd ../jetbrains && gradle buildPlugin verifyPlugin   # build/distributions/mcpp-clion-<version>.zip
```

`xlings install gradle` 会一并带来 Gradle 及其 JDK。

## 文档

- [docs/architecture.md](docs/architecture.md)——三层及其边界
- [protocol/protocol.schema.json](protocol/protocol.schema.json)——后端的方法与通知
- [protocol/mcpp-contract.schema.json](protocol/mcpp-contract.schema.json)——后端读取的 mcpp 契约部分
- [spec/](spec)——设置、命令、翻译：两个编辑器声明的唯一来源
- [vscode/README.md](vscode/README.md)、[jetbrains/README.md](jetbrains/README.md)

## 参与贡献

后端是用 mcpp 构建的 C++23 模块程序；仓库的工具是它的一个程序（`mcpp-ide-dev`：抓取 fixture、生成编辑器声明、
检查翻译）。生成物不提交。mcpp 的输出有变化时，从 `mcpp-ide-dev capture` 与 `protocol/fixtures` 的差异开始。

## 许可证

Apache-2.0
