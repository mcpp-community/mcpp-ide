# mcpp-ide - the IDE layer of mcpp

> One backend for every editor of [mcpp](https://github.com/mcpp-community/mcpp) projects: VS Code
> (mcpp-vscode), CLion (mcpp-clion) and Neovim (mcpp-nvim) build, run, test, debug and pack alike

**English** | [简体中文](README.zh-CN.md)

[Documentation](docs/README.md) · [Protocol](protocol/protocol.schema.json) · [Settings](spec/settings.json) ·
[For agents](agents/README.md) · [Releases](https://github.com/mcpp-community/mcpp-ide/releases)

An editor integration of a build tool decides a great deal: which command a build runs, what a
test result means, how dangerous a cleanup is, which target a project builds for. Written once per
editor, those decisions drift apart, and a person who moves from one editor to another meets a
different tool.

mcpp-ide makes them once. Its backend, `mcpp-ide serve`, is a C++ program that reads only mcpp's
machine contract (event streams, result records, launch descriptors, JSON queries) and answers the
editors over JSON-RPC. The VS Code extension's views and pages are mcpp-vscode 0.9.0's; the CLion
plugin shows the same pages in a workbench of its own (JCEF), and the Neovim plugin maps the answers
onto buffers, floating windows and the quickfix list. The editor plugins are `mide-plugins/` of this repository. Language features of
C++ files come from [mcppls](https://github.com/Sunrisepeak/mcpp-language-server); mcpp-ide
registers none.

## Features

| | VS Code (mcpp-vscode) | CLion (mcpp-clion) | Neovim (mcpp-nvim) |
|---|---|---|---|
| Build, run, test | Tasks in a pseudoterminal showing mcpp's output | "mcpp" run configurations, PTY console, ▶ beside `main` | `:Mcpp build`, `test`: jobs streamed into a buffer; `:Mcpp run` in a terminal buffer |
| Test tree, re-run one test | Testing view | IDE test runner | `:Mcpp test <filter>` |
| Debug | CodeLLDB, C/C++ (gdb, lldb), lldb-dap | CLion's GDB or LLDB | nvim-dap (codelldb, lldb-dap, cppdbg, gdb) |
| Diagnostics | Problems, from mcpp's located diagnostics | Console links to the reported location | `vim.diagnostic`, `:Mcpp problems` in the quickfix list |
| mcpp.toml | Problems from mcpp; completion, hover, outline from mcpp's key registry | Problems from mcpp | Problems from mcpp |
| Libraries, a package's page, graphs, cache, settings | mcpp-vscode's views and pages | The same pages, in a JCEF workbench | Text in floating windows, `vim.ui.select`; settings in `setup()` |
| Targets (target sets among them), pack | Quick picks | Popups | `vim.ui.select` |
| Language features | mcppls (installed with the extension) | mcppls CLion plugin (a dependency) | mcppls-nvim (`xlings install mcppls-nvim`) |

- **One answer everywhere** -- what a build runs, how a test is reported, what a cleanup costs: the backend's
- **Only mcpp's contract** -- no manifest parsing, no dependency resolution of its own
- **Jobs in order** -- one at a time per project, a newer build replaces a waiting one, cancelling ends cleanly
- **Platforms visible** -- each target with the platform that configures it and where every value came from
- **Safe cleanups** -- a plan with a danger level, a preview and the confirmation that level requires

## Install

### From an agent

```
Read agents/README.md of https://github.com/mcpp-community/mcpp-ide, then install mcpp-ide for
the editor I use (VS Code, CLion or Neovim) from its latest release, and check it with
`mcpp-ide self-check` on my mcpp project.
```

### From a release

| Editor | Asset | Install |
|---|---|---|
| VS Code | the Marketplace (`mcpp-community.mcpp-vscode`), or `mcpp-vscode-<version>.vsix` | search *mcpp*, or `code --install-extension mcpp-vscode-<version>.vsix` |
| VS Code-compatible editors | Open VSX, or the `.vsix` | install the `.vsix` from the extensions view |
| CLion 2026.1+ | JetBrains Marketplace (`mcpp-clion`), or `mcpp-clion-<version>.zip` | Settings → Plugins → Install Plugin from Disk; the mcppls CLion plugin (its package for this system carries the server) first |
| Neovim 0.10+ | `mide-plugins/nvim` of the release's source tree | `xlings install mcpp-ide-nvim` (the backend with it, set up at start); or the directory on the runtimepath and `require('mcpp').setup()` ([its README](mide-plugins/nvim/README.md)) |
| Agents, other clients | `mcpp-ide-<version>-openkal-bundle-all.tar.gz` (both images; okrun for Linux x64/arm64, macOS arm64, Windows x64), `mcpp-ide-<version>.okb` with okrun, or `xlings install mcpp-ide` | run `bin/mcpp-ide serve` (`bin\mcpp-ide.exe` on Windows), or `okrun mcpp-ide-<version>.okb serve` |

Each editor package serves every system: it carries the backend as one ok-image bundle
(`backend/mcpp-ide.okb`, the x86_64 and aarch64 images on openkal-platform 0.18.0) and okrun for
Linux x64/arm64, macOS arm64 and Windows x64. The Neovim plugin carries none: it starts the
`mcpp-ide` command xlings installs (okrun on the same bundle).

### From source

```bash
mcpp build                                     # the backend, the mock mcpp, mcpp-ide-dev
mcpp test                                      # the backend's tests
cd mide-plugins/vscode && npm ci && npm test   # the extension's tests, the parity gates against 0.9.0 among them
npm run test:webui                             # the workbench in Chromium, over the backend
npm run package                                # dist/mcpp-vscode-<version>.vsix
cd ../clion && gradle buildPlugin verifyPlugin # build/distributions/mcpp-clion-<version>.zip
cd ../.. && nvim --headless --clean -l mide-plugins/nvim/tests/smoke.lua   # the Neovim plugin's tests
```

`xlings install gradle` brings Gradle and its JDK.

## Documentation

- [docs/architecture.md](docs/architecture.md) -- the three layers and their boundaries
- [protocol/protocol.schema.json](protocol/protocol.schema.json) -- the backend's methods and notifications
- [protocol/mcpp-contract.schema.json](protocol/mcpp-contract.schema.json) -- the part of mcpp's contract it reads
- [spec/](spec) -- settings, commands, translations: the source of the editors' declarations and command tables
- [mide-plugins/vscode/README.md](mide-plugins/vscode/README.md),
  [mide-plugins/clion/README.md](mide-plugins/clion/README.md),
  [mide-plugins/nvim/README.md](mide-plugins/nvim/README.md)

## Contributing

The backend is C++23 with modules, built by mcpp; the repository's tools are a program of it
(`mcpp-ide-dev`: fixture capture, the editors' declarations, the translation check). Generated
files are not committed. A change to what mcpp reports starts with `mcpp-ide-dev capture` and the
diff in `protocol/fixtures`.

## License

Apache-2.0
