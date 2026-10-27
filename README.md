# mcpp-ide - the IDE layer of mcpp

> One backend for every editor of [mcpp](https://github.com/mcpp-community/mcpp) projects: VS Code
> (mcpp-vscode) and CLion (mcpp-clion) build, run, test, debug and pack alike

**English** | [简体中文](README.zh-CN.md)

[Documentation](docs/README.md) · [Protocol](protocol/protocol.schema.json) · [Settings](spec/settings.json) ·
[For agents](agents/README.md) · [Releases](https://github.com/mcpp-community/mcpp-ide/releases)

An editor integration of a build tool decides a great deal: which command a build runs, what a
test result means, how dangerous a cleanup is, which target a project builds for. Written once per
editor, those decisions drift apart, and a person who moves from one editor to another meets a
different tool.

mcpp-ide makes them once. Its backend, `mcpp-ide serve`, is a C++ program that reads only mcpp's
machine contract (event streams, result records, launch descriptors, JSON queries) and answers the
editors over JSON-RPC. The VS Code extension and the CLion plugin map those answers onto their own
UI and share one set of web panels. Language features of C++ files come from
[mcppls](https://github.com/Sunrisepeak/mcpp-language-server); mcpp-ide registers none.

## Features

| | VS Code (mcpp-vscode) | CLion (mcpp-clion) |
|---|---|---|
| Build, run, test | Tasks in a pseudoterminal showing mcpp's output | "mcpp" run configurations, PTY console, ▶ beside `main` |
| Test tree, re-run one test | Testing view | IDE test runner |
| Debug | CodeLLDB, C/C++ (gdb, lldb), lldb-dap | CLion's GDB or LLDB |
| Diagnostics | Problems, from mcpp's located diagnostics | Console links to the reported location |
| mcpp.toml | Problems from mcpp; completion, hover, outline from mcpp's key registry | Problems from mcpp |
| Targets and platforms, libraries, graphs, cache, pack, settings | Shared web panels | The same panels (JCEF) |
| Language features | mcppls (installed with the extension) | mcppls CLion plugin (a dependency) |

- **One answer everywhere** -- what a build runs, how a test is reported, what a cleanup costs: the backend's
- **Only mcpp's contract** -- no manifest parsing, no dependency resolution of its own
- **Jobs in order** -- one at a time per project, a newer build replaces a waiting one, cancelling ends cleanly
- **Platforms visible** -- each target with the platform that configures it and where every value came from
- **Safe cleanups** -- a plan with a danger level, a preview and the confirmation that level requires

## Install

### From an agent

```
Read agents/README.md of https://github.com/mcpp-community/mcpp-ide, then install mcpp-ide for
the editor I use (VS Code or CLion) from its latest release, and check it with `mcpp-ide self-check`
on my mcpp project.
```

### From a release

| Editor | Asset | Install |
|---|---|---|
| VS Code | the Marketplace (`mcpp-community.mcpp-vscode`), or `mcpp-vscode-<version>.vsix` | search *mcpp*, or `code --install-extension mcpp-vscode-<version>.vsix` |
| VS Code-compatible editors | Open VSX, or the `.vsix` | install the `.vsix` from the extensions view |
| CLion 2025.2+ | JetBrains Marketplace (`mcpp-clion`), or `mcpp-clion-<version>.zip` | Settings → Plugins → Install Plugin from Disk; the mcppls CLion plugin first |
| Agents, other clients | `mcpp-ide-<version>-<platform>.tar.gz`, or `xlings install mcpp-ide` | run `mcpp-ide serve` |

Both editor packages carry the backend for Linux, macOS and Windows.

### From source

```bash
mcpp build                        # the backend, the mock mcpp, mcpp-ide-dev
mcpp test                         # the backend's tests
cd vscode && npm ci && npm test   # the extension's and the panels' tests
npm run package                   # dist/mcpp-vscode-<version>.vsix
cd ../jetbrains && gradle buildPlugin verifyPlugin   # build/distributions/mcpp-clion-<version>.zip
```

`xlings install gradle` brings Gradle and its JDK.

## Documentation

- [docs/architecture.md](docs/architecture.md) -- the three layers and their boundaries
- [protocol/protocol.schema.json](protocol/protocol.schema.json) -- the backend's methods and notifications
- [protocol/mcpp-contract.schema.json](protocol/mcpp-contract.schema.json) -- the part of mcpp's contract it reads
- [spec/](spec) -- settings, commands, translations: the source of both editors' declarations
- [vscode/README.md](vscode/README.md), [jetbrains/README.md](jetbrains/README.md)

## Contributing

The backend is C++23 with modules, built by mcpp; the repository's tools are a program of it
(`mcpp-ide-dev`: fixture capture, the editors' declarations, the translation check). Generated
files are not committed. A change to what mcpp reports starts with `mcpp-ide-dev capture` and the
diff in `protocol/fixtures`.

## License

Apache-2.0
