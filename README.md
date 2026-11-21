<div align="center">

# mcpp-ide

**One backend for every editor of [mcpp](https://github.com/mcpp-community/mcpp) projects**

VS Code · CLion · Neovim build, run, test, debug and pack alike

[![Release](https://img.shields.io/github/v/release/mcpp-community/mcpp-ide)](https://github.com/mcpp-community/mcpp-ide/releases)
[![CI](https://github.com/mcpp-community/mcpp-ide/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpp-community/mcpp-ide/actions/workflows/ci.yml)
[![VS Code](https://img.shields.io/badge/VS_Code-mcpp--vscode-007ACC?logo=visualstudiocode)](https://marketplace.visualstudio.com/items?itemName=mcpp-community.mcpp-vscode)
[![Open VSX](https://img.shields.io/open-vsx/v/mcpp-community/mcpp-vscode?label=Open%20VSX)](https://open-vsx.org/extension/mcpp-community/mcpp-vscode)
[![JetBrains](https://img.shields.io/jetbrains/plugin/v/io.github.mcpp-community.mcpp-clion?label=CLion)](https://plugins.jetbrains.com/plugin/io.github.mcpp-community.mcpp-clion)
[![C++23](https://img.shields.io/badge/C%2B%2B-23_modules-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/license-Apache_2.0-blue.svg)](LICENSE)

**English** | [简体中文](README.zh-CN.md)

[Documentation](docs/README.md) · [Architecture](docs/architecture.md) · [Protocol](protocol/protocol.schema.json) ·
[For agents](agents/README.md) · [Contributing](AGENTS.md) · [Releases](https://github.com/mcpp-community/mcpp-ide/releases)

</div>

An editor integration of a build tool makes many decisions: which command a build runs, what a test
result means, how dangerous a cleanup is. If each editor makes them separately, the editors drift
apart. mcpp-ide makes them once. Its backend, `mcpp-ide serve`, reads only mcpp's machine contract
and answers every editor as an LSP server. Each plugin then shows those answers in its editor's own UI.

```
mcpp ──contract──▶ mcpp-ide serve ──LSP / JSON-RPC──▶ mcpp-vscode · mcpp-clion · mcpp-nvim · agents
                   (every decision)                   (native UI, no decisions)
mcppls ──────────────────────────────────────────────▶ C++ language features in every editor
```

## Highlights

- **One answer everywhere**: what a build runs, how a test is reported and what a cleanup costs are decided by the backend
- **Only mcpp's contract**: no manifest parsing and no dependency resolution of its own
- **Tests as mcpp states them**: programs, cases, live results, diffs, metrics and Test Insights
- **Jobs in order**: one build at a time per project, a newer build replaces a waiting one, and cancelling ends cleanly
- **Safe cleanups**: each plan has a danger level, a preview and the confirmation that level requires

## Editors

| | VS Code | CLion | Neovim |
|---|---|---|---|
| Build, run, test | tasks, Testing view | run configurations, test runner, ▶ beside `main` | `:Mcpp`, buffers, quickfix |
| Debug | CodeLLDB, C/C++, lldb-dap | CLion's GDB / LLDB | nvim-dap |
| mcpp.toml, build.mcpp | completion, hover, jumps, diagnostics | the same | the same, over `vim.lsp` |
| Libraries, graphs, cache, settings | views and pages | native trees, forms, JCEF graphs | floating windows, `vim.ui.select` |
| C++ language features | mcppls (extension pack) | mcppls CLion plugin | mcppls-nvim |
| Details | [mcpp-vscode](mide-plugins/vscode/README.md) | [mcpp-clion](mide-plugins/clion/README.md) | [mcpp-nvim](mide-plugins/nvim/README.md) |

## Install

| Editor | Install |
|---|---|
| VS Code 1.91+ | Marketplace or Open VSX: `mcpp-community.mcpp-vscode`, or `code --install-extension mcpp-vscode-<version>.vsix` |
| CLion 2026.1+ | JetBrains Marketplace: `mcpp-clion`, which depends on the mcppls CLion plugin; or install the ZIP from disk |
| Neovim 0.10+ | `xlings install mcpp-ide-nvim` (with the backend; it sets itself up at start) and `xlings install mcppls-nvim` |
| Agents, other clients | `xlings install mcpp-ide`, then `mcpp-ide serve`; or `okrun mcpp-ide-<version>.okb serve` |

The VS Code and CLion packages carry the backend for Linux x64/arm64, macOS arm64 and Windows x64
([distribution](docs/architecture.md#distribution)). Run `mcpp-ide self-check` in a project to verify an
install. Versions and compatibility with mcpp are described in [docs/versions.md](docs/versions.md).

## With an agent

A good prompt names **the repository**, **the document to read first**, **the task** and **the check
that ends it**. The documents below are written for agents, so the prompt can stay short.

| Goal | Prompt |
|---|---|
| Install | `Read agents/README.md of https://github.com/mcpp-community/mcpp-ide, install mcpp-ide for my editor from the latest release, and verify it with mcpp-ide self-check on my project.` |
| Understand | `Read AGENTS.md and docs/architecture.md of mcpp-ide, then explain how a build travels from my editor through the backend to mcpp, and which layer owns <feature>.` |
| Contribute | `Read AGENTS.md and .agents/skills/mcpp-ide-contributing of mcpp-ide. Triage <issue>: core or plugin, fix or workaround. Then propose the change and the tests that prove it, and wait for my go-ahead before opening a pull request.` |

- Using mcpp-ide from an agent: [agents/README.md](agents/README.md) and the [mcpp-ide skill](agents/skills/mcpp-ide/SKILL.md)
- Working on mcpp-ide: [AGENTS.md](AGENTS.md), the [contributing](.agents/skills/mcpp-ide-contributing/SKILL.md) and [development](.agents/skills/mcpp-ide-development/SKILL.md) skills

## Development

```bash
mcpp build && mcpp test                                      # the backend, its mocks and mcpp-ide-dev
(cd mide-plugins/vscode && npm ci && npm test)               # VS Code, the parity gates included
(cd mide-plugins/clion && gradle buildPlugin verifyPlugin)   # CLion (xlings install gradle)
nvim --headless --clean -l mide-plugins/nvim/tests/smoke.lua # Neovim
```

The environment, the tests for each component, common changes and the release procedure are in the
[development skill](.agents/skills/mcpp-ide-development/SKILL.md).

## Contributing

Every change starts with an issue ([templates](https://github.com/mcpp-community/mcpp-ide/issues/new/choose))
and goes through a pull request. The rules are in [AGENTS.md](AGENTS.md): who owns what, how to
triage (core or plugin, fix or workaround), versions and code. The procedure for people and agents
is in the [contributing skill](.agents/skills/mcpp-ide-contributing/SKILL.md). Questions go to the
[forum](https://forum.d2learn.org/category/20).

## License

[Apache-2.0](LICENSE)
