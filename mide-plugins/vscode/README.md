# mcpp for Visual Studio Code

**English** | [简体中文](https://github.com/mcpp-community/mcpp-ide/blob/main/README.zh-CN.md)

Build, run, test, debug and pack [mcpp](https://github.com/mcpp-community/mcpp) C++23 module
projects, browse their libraries, dependency and module graphs, targets and platforms, and manage
mcpp's shared build cache.

This extension is the VS Code adapter of [mcpp-ide](https://github.com/mcpp-community/mcpp-ide).
Its decisions are made by the mcpp-ide backend (`mcpp-ide serve`), which reads only mcpp's
machine contract; JetBrains CLion has the same features through mcpp-clion, and Neovim through
mcpp-nvim.

## Language features come from mcppls

Completion, navigation, highlighting and diagnostics of C++ files come from the
[C++ Modules language server](https://marketplace.visualstudio.com/items?itemName=sunrisepeak.mcpp-language-server)
(mcppls), which this extension installs with it (an extension pack: it can be uninstalled
separately). This extension registers no language service. mcppls 0.0.18 and later refresh after
every build, whoever runs it; the project view says when the installed mcppls is older or missing.

## Features

| Feature | Where |
|---|---|
| Build and test | Tasks of type `mcpp` in a terminal that shows mcpp's own output, in colour; closing it cancels the build |
| Run | The program in a terminal of its own, started the way `mcpp run` starts it |
| Debug | A launch configuration of type `mcpp`; the debugger is CodeLLDB, the C/C++ extension, lldb-dap or Native Debug. An ok image (`--target <arch>-openkal`) is debugged through `okrun` |
| Tests | The Testing view: every test of every project, run one or all |
| Problems | mcpp's diagnostics, located where mcpp reports them; mcpp.toml's problems from mcpp itself |
| Targets and platforms | The targets mcpp knows, their toolchains and the platform that configures each; the build target is chosen in the status bar |
| Libraries | The packages of mcpp's indexes; `mcpp add` from a package's page |
| Graphs | The package dependency graph and the module graph of a project |
| Cache | The shared build cache by kind, package and age; cleanups with a preview and a confirmation at their level of danger |
| Pack | The formats a project can be packed into, and the result |
| mcpp.toml | Completion, hover and outline from mcpp's own key registry |

## Requirements

- mcpp 2026.10.26.1 or later (`xlings install mcpp`).
- The mcpp-ide backend: shipped inside the extension for every system (one ok-image bundle run by
  the okrun the extension carries for Linux x64/arm64, macOS arm64 and Windows x64, or by okrun on
  PATH elsewhere); otherwise `xlings install mcpp-ide`, or set `mcpp.ide.backendPath` (a program or
  an `.okb`).

## Upgrading from 0.9

The extension ID is unchanged (`mcpp-community.mcpp-vscode`), and so are its command IDs and
setting keys. Settings that described the removed TOML checks are kept and marked deprecated; the
problems of mcpp.toml now come from mcpp.

## Untrusted workspaces

In an untrusted workspace mcpp runs with the effects listed in `mcpp.trust.denyEffects` denied
(`mcpp --deny`): a command that needs one of them stops before it and says so.

## License

Apache-2.0
