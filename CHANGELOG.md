# Changelog

## 0.2.0

mcpp-vscode 1.1.0, mcpp-clion 0.2.0 and mcpp-nvim 0.2.0 (mcpp design 2026-10-29 §10).

- **The project's mcpp (mcpp design 2026-10-30 part 2 §4).** Each project is asked in its own
  directory, `mcpp self env --format json` through the program a version manager's shim stands for
  (or `mcpp.path`), with `XLINGS_PROJECT_DIR` removed from every run of mcpp, so a project pinned in
  `.xlings.json` is described by the mcpp that builds it. A feature is turned on by what the project's
  mcpp states (`features`, mcpp 2026.10.31.1) or, for an earlier mcpp, by a table frozen at that
  release, never by comparing versions. A pinned version that is not installed is `pinned-missing`,
  with the shim's words and its install command; an untrusted workspace's project is not asked. A
  job whose envelope names another version, the editor regaining focus and a changed manifest each
  make the project be asked again; never more than two at once. Methods `project/mcpp`,
  `project/refresh`, `capabilities/explain` (why a feature is not offered: which party, which
  capability, from which version) and `report/collect`; notification `project/mcppChanged`.
- **The backend's lifecycle and what it says (§3).** VS Code and CLion start an ended backend again
  after 1, 4 and 16 seconds; a fourth end within three minutes leaves them in the degraded mode with
  two actions, restarting the backend and exporting the IDE report. **mcpp: Export IDE Report**
  (every editor; Neovim `:Mcpp exportReport [path]`) writes the backend, every project's mcpp,
  mcppls's status, the settings in effect, recent jobs and the backend's recent log to a file the
  person names. `mcpp.backend.logLevel` decides what the backend writes to the editor's log. VS
  Code's status item names the project's mcpp version, and its tooltip the program and whether
  `mcpp.path` chose it.
- **What mcppls says about the workspace (§5).** The backend's `languageService/status` runs
  `mcppls status --root <dir> --format json` (mcppls 0.0.20 and later, bounded at five seconds) and
  returns its capabilities, the workspace's state, engine and kit, the servers serving it, and
  mcppls's actions under each editor's own id; for mcppls 0.0.18 and 0.0.19 the actions come from a
  table frozen at those releases. A different answer is sent once (`languageService/changed`). The
  command table's forwards name mcppls's action ids, and `commands/list` gives each editor the id
  mcppls states for it: CLion runs the mcppls plugin's action (and hides a row it does not have),
  Neovim mcppls-nvim's command. VS Code reads the state and the commands from this answer instead of
  the mcppls extension's API object and keeps only the confirmation each action needs; the state is
  asked again after a job, on focus, when the project view becomes visible and, while it is visible,
  at most every 30 seconds. A missing semantic kit is a row with **C++ Modules: Install the Semantic
  Kit**. After a build nothing is forwarded by default (`auto`): mcppls reads mcpp's result record.
- **Entries (§6).** The backend's `source/entries` says what a C++ file is to its project -- a
  program's `main`, a test's (`tests/**`, named as mcpp names it), or neither -- with the line of
  `main` read from the text, outside comments and literals. VS Code shows Run and Debug above `main`
  (`mcpp.codeLens.enable`), at the position C++ Modules gives and otherwise at the line read, said to
  be approximate; Build and Debug join Run and Test in the editor's title bar, and Run, Debug, Build
  and Test are in the editor's context menu. The command table states these places (`placements`),
  and the generator writes VS Code's menus and CLion's editor popup from it. CLion's ▶ stands on the
  PSI element `main` on the line the backend read, the IDE's Build (the hammer) of an mcpp project is
  `mcpp build`, a status bar item shows the project's mcpp and the running job with the quick menu,
  and two keymaps (`mcpp`, `mcpp (macOS)`) carry VS Code's chords.
- **Test cases (§7).** `spec/test-frameworks.json` describes GoogleTest, Catch2 and doctest: a test
  program belongs to one when its plan holds the framework's package and its source calls the
  framework's macros (`mcpp.test.macros` adds a project's own). `test/cases` reads the cases from the
  source and, after a build in a trusted workspace, from the program's own listing; `test/runCases`
  runs some of them with the framework's filter and reads its report, each outcome sent as
  `test/caseResult`. VS Code's Testing view has the cases under their program, located at their
  macros, and the program at its `main`; CLion has a ▶ on each case and runs cases in its test tree.
  Debugging a test's program is offered where the project's mcpp describes its launch (`test-launch`,
  mcpp 2026.10.31.2).
- **The rules layer of mcpp.toml and build.mcpp, in the backend (§8; docs/rules.md).** The session
  is an LSP server, protocol 2: `initialize` in LSP's shape with mcpp-ide's parameters in
  `initializationOptions`, mcpp.toml's and build.mcpp's completion, hover, jumps and diagnostics as
  `textDocument/*` (full synchronisation, UTF-16 columns), and mcpp-ide's own methods and
  notifications under `mcpp-ide/`. The tolerant scanner, the seven manifest rules and the SPEC-007
  checks are ported from mcpp-vscode 0.9's TypeScript, whose answers over a corpus at every position
  are the tests' goldens (`tests/rules`). A table is read through the schema (a workspace table as the
  table it mirrors, a platform's table under a selector as the platform's); a selector-only table is
  offered with its selector; the schema's own arrays of tables are allowed; a dependency's version is
  completed from mcpp's catalogue (`mcpp search --format json`) when `mcpp.toml.indexCompletion` is
  on. VS Code connects with its language client (bundled into `dist/vendor`); CLion over the
  session's connection, through a completion contributor, an annotator, a documentation provider
  and a go-to-declaration handler; Neovim with its LSP client, attached to mcpp.toml and build.mcpp
  buffers. While C++ Modules serves build.mcpp (`mcpplsServes`) it is left the unknownSymbol rule
  and the module names after `import`. One snapshot of each: `spec/toml-schema.json`,
  `spec/buildscript-api.json` (mcpp 2026.10.29.1's directives).
- **CLion in its own components (§9).** The "mcpp" tool window's Project, Libraries and Cache tabs
  are native trees and lists over the backend's view models (each project's mcpp, targets, tests,
  libraries, last build, artifacts and platform, and what C++ Modules says; the indexes' packages, a
  package's page and adding it to the project; what mcpp keeps and the command table's cleanups with
  their confirmations). A graph of the packages or the modules is the one page in JCEF
  (`webui/graph.html`, mermaid over `graph/packages` and `graph/modules`; a module opens its source),
  tested in Chromium over a real backend. Settings | Tools | mcpp is a form generated from
  `settings/registry`, with Apply, Reset and a search. The workbench that ran mcpp-vscode's pages in
  JCEF, its `vscode` shim and its bridge are removed; the views' models and the settings form are
  tested headless.
- **The declared degraded mode (§8, F15).** Where the package's okrun does not run (macOS x86_64,
  Windows arm64, Linux armhf) VS Code says the backend does not run on this platform and goes on
  with build, run, test and the project pages, running mcpp's queries itself; an Extension Host run
  in CI holds it.
- **mcpp-vscode 1.1.0 is mcpp-vscode 0.9.0 over the backend.** 0.9.0's extension is imported with
  its history (`mide-plugins/vscode`); every view, page, command, menu, setting and string a user
  sees is 0.9.0's. What 1.0 added stays, in 0.9.0's style: debugging (`mcpp.debug`, the `mcpp`
  launch type), the test tree, the target picker with target-set rows, packing, build on save,
  cancellable jobs. mcpp is run through the backend's `mcpp/exec`, mcppls through its
  `languageService/exec`; the extension starts no other process for mcpp or mcppls.
- **Parity gates against 0.9.0.** The manifest's contributions and strings contain 0.9.0's exactly
  (`spec/` states them; `mcpp-ide-dev gen vscode` writes the manifest); every page's document,
  rendered by the extension's generators, is byte-identical to the snapshot rendered by 0.9.0's;
  the Extension Host suite runs over the bundled backend.
- **One page library for VS Code and CLion.** The workbench (`webui/`) runs mcpp-vscode's page
  modules unchanged in a browser host: the `vscode` module and Node's built-ins are answered by
  the host's bridge, views are tabs, webviews are iframes with the IDE's `--vscode-*` theme.
  mcpp-clion's tool window and its settings page are this workbench in JCEF; the earlier panels
  (`webui/js`) are removed. A Chromium test runs the workbench over a real backend.
- **Backend methods.** `mcpp/exec` (a bounded mcpp run: timeout, output limit, truncation reported),
  `languageService/exec` (the located mcppls with arguments), `project/files` (manifest, lock and
  the source count 0.9.0 shows); `targets` rows for target sets (`set`, `members`, `label`) when mcpp
  is 2026.10.30.1 or later.
- **The mcpp logo in CLion.** mcpp-clion's plugin icon and its tool window's icon are the mcpp
  logo, as mcpp-vscode's and mcppls's are; the text icons (`m++`, `m+`) are removed. CI checks that
  both embed the logo and that the VS Code logo is 0.9.0's.
- **mcpp-clion on 2026.1 through 2026.3.** Trust through `TrustedProjects.isProjectTrusted`, debug
  sessions through `XDebuggerManager.newSessionBuilder`, the debugger driver through
  `createDriverConfiguration(project, CPPEnvironment(toolchain))`; packing is a job over the
  backend's `pack/formats`.
- **mcpp 2026.10.31.2.** One pack of the target set `openkal` writes the editor packages' tree and
  okb (`--target openkal --mode self-contained --systems all --pkg-format tar,okb`); CI reads
  `runs_on`; the Windows launcher is `bin/mcpp-ide.exe`, built over a platform other than mcpp's own
  openkal rows'. The backend is on mcpplibs.os 0.2.4 and openkal-platform 0.19.0, ok images included
  (openkal 0.18, whose okrun maps the image it runs).
- **CI and release.** 0.9.0's gates run in this repository (registry, translations, unit suite on
  three systems, manifest drift, parity, Extension Host on three systems, isolated install, the
  graph page in Chromium); the release publishes the VSIX to Open VSX beside the GitHub release.

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
