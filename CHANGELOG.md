# Changelog

## 0.1.1

mcpp-vscode 1.0.1, mcpp-clion 0.1.1 and the first mcpp-nvim (0.1.1), with mcppls 0.0.19.

- **mcppls is found as the command that starts it.** mcppls 0.0.18 and later carry the server as an
  okb that the platform's runner starts (`payload.json`'s `server`: `bin/okrun bin/mcppls.okb`). The
  backend ran the okb itself for `--version`, so the VS Code extension's mcppls was reported as not
  reporting its version (`permission denied`), and the payload in the user data directory was looked
  for as `bin/mcppls`. A candidate is now a payload directory, whose `server` is started as it says,
  or a program; `languageService/status` reports the `command` beside the `program`. What
  `server` names is followed only inside the payload (a relative path with no `..` part), as mcppls
  reads it.
- **VS Code**: the candidate is the mcppls extension's payload directory (`MCPPLS_PAYLOAD`, else
  `<extension>/payload`), and `MCPPLS_SERVER`.
- **CLion**: the candidate is the payload the mcppls CLion plugin carries (mcppls 0.0.19 ships one
  package per system, each with its server), with its runner made executable again when the
  plugin's install dropped the bit, and `MCPPLS_SERVER`. *Install mcppls* and *Update
  mcppls* open Settings | Plugins, where the Marketplace gives this system's package.
- **CLion 2026.1 and later** (since-build 261), as the mcppls plugin mcpp-clion depends on; the
  Plugin Verifier checks 2026.1 and 2026.2.3, and the build loads this machine's mcppls 0.0.19
  package.
- **The editor plugins are `mide-plugins/`**: `mide-plugins/vscode` (mcpp-vscode),
  `mide-plugins/clion` (mcpp-clion; the protocol's editor id stays `jetbrains`, the IntelliJ
  platform's) and `mide-plugins/nvim` (mcpp-nvim). The backend, the panels, `spec/`, `protocol/` and
  the tests stay at the root; both earlier adapters build and test as before.
- **mcpp-nvim, mcpp's project features in Neovim 0.10 and later**, over the same backend through
  Neovim's own JSON-RPC client: `:Mcpp <subcommand>` with completion, from the backend's command
  table for the editor `nvim`; builds, tests, cleanups and packs as jobs streamed into
  `mcpp://output`, cancelled with `:Mcpp cancel`; the program in a terminal buffer; debugging through
  nvim-dap when it is present; targets, toolchains, new projects and libraries through
  `vim.ui.select` and `vim.ui.input`; targets and platforms, the cache and the graphs as text in
  floating windows; mcpp's diagnostics in the namespaces `mcpp` and `mcpp.toml`, and
  `:Mcpp problems` in the quickfix list; `require('mcpp').status()` for the statusline. It registers
  no language service: mcppls comes with mcppls-nvim (`xlings install mcppls-nvim`), which the plugin
  names when mcppls is missing or older. The backend is `mcpp-ide` on PATH
  (`xlings install mcpp-ide`) or setup()'s `backend`; `xlings install mcpp-ide-nvim` installs the
  plugin with the backend and sets it up at Neovim's start (`vim.g.mcpp_auto_start = false` opts
  out). A backend that ends is said; the requests and jobs waiting on it fail, and the next command
  starts another, which watches the same projects. A headless suite runs it against the backend on
  Neovim 0.10.4 and 0.12.5, over the mock mcpp and over the real one.
- **The protocol's editor `nvim`**, and each row of `commands/list` names its `subcommand` (the id
  after `mcpp.`, or the row's own: `mcpp.cancelJob` is `cancel`). `spec/commands.json` says why an
  editor is left out of a row (`omitted`); Neovim's table has `mcpp.problems` (the action `problems`)
  and the quick menu, and leaves out the settings panel and the panels' entry pages. Which rows an
  editor offers is one function, shared by the backend and `mcpp-ide-dev gen`.
- **The cache inventory's rows carry their size as text** (`bytesText`), for an editor that shows it
  as text.

## 0.1.0

The first release of mcpp-ide: one backend for mcpp's editor integrations, the VS Code extension
(mcpp-vscode 1.0.0, the same extension ID as before) and the CLion plugin (mcpp-clion 0.1.0).

- Backend (`mcpp-ide serve`): sessions, watching, jobs, view models, cleanup plans, settings, the
  command table, runtime strings, self-check; handshakes with mcpp and mcppls; targets with their
  platforms.
- Protocol and contract schemas, held against mcpp 2026.10.27.1's real output (target rows naming
  their platform, a plan's platform and host platform, a wrapped program's launch).
- Shared panels: project, targets and platforms, libraries, dependency and module graphs, cache,
  pack, settings.
- VS Code: tasks, Testing view, debug configurations, Problems from mcpp, mcpp.toml editing help.
- mcpp.toml and build.mcpp editing help over every table and key mcpp 2026.10.27.1 reads, each with
  the release that added it: `[platform]` and its tables, `[host]`, a selector's platform overrides
  (`platform`, `carrier`, `layers`, `tools`, `link`, `runners`, `run_under`) and its ABI tables
  (`abi`, `requires_abi`, `feature-requires-abi`), `[pack]` and `[pack.bundle-project]`, `[c-abi]`,
  `[kernel-abi]`, `[runtime]` and its entry tables, `[hooks]` and each event's table, a feature's
  table, `[xlings]`, `[workspace]`; the protocol 17 directives, completed by the typed API's names
  (`mcpp::link_driver`).
- CLion: run configurations with a PTY console, test tree, CLion's debugger, console links, the mcpp
  tool window; depends on the mcppls CLion plugin.
- One package per editor for every system: the backend as one ok-image bundle (x86_64 and aarch64
  images on openkal-platform 0.17.3) with okrun for Linux x86_64/aarch64, macOS arm64 and Windows
  x64; CI starts it from both packages on each of those systems.
- `mcpp pack`'s package format is passed as `--pkg-format` where mcpp offers it (`--format` before).
- The operating system through mcpplibs.os 0.2.1 (an index version), whose ok-image targets take
  their C library, C++ run-time and loader from openkal-platform.
