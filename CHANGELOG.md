# Changelog

The changelog is kept by series (`X.Y`); each section lists its components' releases.

## 1.3

### 1.3.0

Every component is 1.3.0: the backend, mcpp-vscode, mcpp-clion and mcpp-nvim (from 1.2.1, 1.2.0, 1.2.0
and 1.2.0).

- **A change of the backend begins a series.** The VS Code and CLion packages carry the backend, so a
  change of it is a change of every package: it is released as `vX.Y.0`, every component at `X.Y.0`,
  and the backend is always its series' `X.Y.0`. `Z` is an editor plugin's own patch (`vscode-`,
  `clion-`, `nvim-vX.Y.Z`), which carries its series' backend. The release refuses a `backend-v` tag,
  a backend that is not `X.Y.0`, and a plugin's patch when a path of the backend has changed since its
  series' tag. Backend 1.2.1 reached `xlings install mcpp-ide` alone; its change, mtest's cases read
  with mtest's own static manifest, is in every package of 1.3.0.
- **Tests as mcpp states them** (design 2026-11-10). A program of several files (`[test] programs =
  "directory"`, mcpp 2026.11.9.1) is shown with its files as children, each the part of the program a
  run gives a process (`unit/a` of `unit`), and a part's record is attributed to its program; 1.2 listed
  `unit` and lost the results of `unit/a` and `unit/b`. A case carries its file, kind, tags, expectation
  and the bounds on its metrics, from mtest's manifest and the program's listing. A whole run marks every
  case of a protocol program, and a run of cases reports every part's record, not only the last.
  `xfail` passes, `xpass` fails as an unexpected pass, and `invalid` is errored. A failure that states
  both sides is a diff in VS Code, a comparison failure in CLion, and two lines in Neovim's buffer and
  quickfix item.
  - **Live results.** With mcpp's `test-case-stream` (mcpp#917), each case is shown as it begins and
    ends (`test/caseEvent`).
  - **Metrics.** With `test-metrics`, a case's metrics are shown against their bounds: at the end of its
    line in VS Code and Neovim, and in its output in CLion.
  - **Test Insights** (`mcpp.test.insights`; `test/insights`, `test/history`). It reads the runs mcpp
    keeps in `target/mtest`: the last run and its counts by kind; its failures and since when; each
    metric's trend against its bound; the slowest and the flaky cases; the schedule with its critical
    path. VS Code shows it in a webview, CLion in the mcpp tool window (both draw `webui/tests.html`),
    and Neovim as text.
  - **Older mcpp.** A view that needs a feature the project's mcpp does not state is left out, and
    `capabilities/explain` names the feature (`tests.cases`, `tests.parts`, `tests.live`,
    `tests.metrics`).
  - **Speed.** The suite types of a project's tests are read again only when its test files change.
  - **CLion.** The test tree is ID-based, so parts that run at once report into their own suites.
- **The backend's tests are mtest's** (mtest 0.5.2). `tests/unit`, `tests/e2e`, `tests/cts` and
  `tests/bench` are each one program whose files declare their cases, with no `main`; the rules
  goldens and the published payloads sit beside the cases that read them. `tests/bench` bounds the
  latency of a keystroke in a manifest of 400 dependencies, of reading a test file of 2000 cases, and
  of the insights over 50 runs of 400 cases. testsupport's `check` is mtest's fatal expectation, and
  CI reports each run with `mtest report` in the job's summary.
- **Dependencies.** The backend builds with mcpp 2026.11.9.1, mcpplibs.os 0.4.2 and
  mcpplibs.mtest.manifest 0.5.2.
- **mcppls 0.21.** mcpp-clion is built and tested against the mcppls CLion plugin 0.21.0, whose one
  package serves every system (`mcppls-clion-<version>.zip`, since mcppls 0.0.20); the build fetched
  0.0.19's package of this machine. mcppls 0.21 numbers its versions as a series and a patch (0.0.21
  is released as 0.21.0); the backend reads that version, its payloads (version 5) and `mcppls status`
  as before.

## 1.2

### backend 1.2.1

- **mtest's cases as mtest reads them.** The cases of a file of mtest are read with mtest's own
  static manifest (`mcpplibs.mtest.manifest` 0.4.1) instead of a reader of the backend's: an object
  (`UTest parse{"parse", [] { ... }}`, and `Suite`, `CTS`, `E2E`, `Bench` or a suite type derived from
  one) is a case, and so is each `"name"_test` it declares, under its name. A suite type the project's
  tests declare in a module beside them (`class Fixture : public CTS` in `tests/cts/cts.cppm`) is read
  from `tests/` and holds in every file that uses it. A file written for mtest 0.3 or 0.4 shows its
  cases before it is built, as one written for 0.2 did.

### 1.2.0

The first series of one version for every component: the backend, mcpp-vscode, mcpp-clion and
mcpp-nvim are 1.2.0 (from 0.2.1, 1.1.1, 0.2.1 and 0.2.1).

- **One series.** A version is `X.Y.Z`; `X.Y` is shared by the backend and every plugin, `Z` is each
  component's own patch. The backend states its series in the initialize answer, and each plugin
  checks protocol 2 and its series and refuses another, naming both versions and the install that
  matches. A series release is tagged `vX.Y.0`; `backend-`, `vscode-`, `clion-` and `nvim-vX.Y.Z`
  publish one component's patch, and CI checks that every component states one series.
- **mcppls payloads by their specification.** The backend reads `payload.json` as mcppls's S6 states
  it: version 5 (mcppls 0.0.20 and later) takes this system's entry under `platforms` and starts
  `<runner> <server.path>`; version 4 serves the one `platform` it names. A payload that does not
  serve this system is refused, naming the platforms it does serve. 0.2.1 ran a version 5 payload's
  `bin/mcppls.okb` itself and failed. CLion makes every runner a payload names executable again.
- **Test results as mcpp states them.** Statuses follow mcpp's test records value for value:
  `run_fail` is failed, or timed out when mcpp says so; `compile_fail` is failed and says it did not
  compile; `not_run` is skipped with mcpp's reason; `built` is skipped as built only. 0.2.1 mapped
  values mcpp never writes, so every failure was "errored" and a timeout was never seen.
- **Cases by the test protocol.** A test program that implements the mtest test protocol (mcpp
  2026.11.8.1, feature `test-protocol`) is listed by `mcpp test --cases` and its cases run by
  `mcpp test --case`, whatever framework wrote it; each case's result, duration and first failure's
  place come from mcpp's record. A file of mtest's cases shows its `"name"_test` literals, named
  with their suites (`parsing/an empty line`), before anything is built. GoogleTest, Catch2 and
  doctest without the protocol are listed and run as before (`spec/test-frameworks.json`).
- **Every test's own output.** Each test and each case carries its whole output, passed or not
  (head and tail past 1 MiB), and a failure carries the place its output names. A case run divides
  the program's output among its cases by GoogleTest's boundaries. VS Code says the output to the
  test's own item and shows a failure at its place, else at the test's `main` or the case's macro;
  CLion sends it to the test tree, with the place first in a failure's details; Neovim writes
  `mcpp://tests`, one section per test and case, and puts the failures in the quickfix list.
- **Run entries with icons.** VS Code's lenses above `main` are `$(play) Run` and `$(debug-alt)
  Debug`, and while the active editor is a target's entry the editor title's Run menu runs and
  debugs it. Neovim's sign column shows ▶ on a program's and a test's `main` and on each case, and a
  result turns it into ✓ or ✗. CLion logs why a file has no run entry.
- **The platform as one release.** The payloads' platform is `openkal-platform@0.24.4`, whose okrun
  enters an image in its convention's floating-point state (a decimal number is read on Windows as on
  the other systems), and every dependency is one release
  named by its identity (`mcpplibs.os = "0.4.0"`); mcpp 2026.11.7.1 or later builds them.
- **A Run shows its build as the terminal does.** Run and Debug build through a launch job: mcpp's
  own narration (Compiling, Finished) appears as it is written, in place of a fixed "building" line,
  and the build's steps in the status bar (CLion, VS Code while it builds for the debugger, the
  Neovim statusline), as for a Build. With mcpp 2026.11.7.2 an unchanged program's launch is
  described on mcpp's fast path, 0.01 s where it was 0.1 to 0.3 s, so the program starts at once.
  CLion records how long the build and the program's start took in `idea.log`.
- **Windows names reach the backend in one spelling.** The backend runs as an ok image, whose names
  are `/`-separated on every system; a project root, a working directory, a parent directory or a
  payload an editor names as `C:\Users\x\…` is read through mcpplibs.os into its generic spelling
  before anything is built from it, so it is absolute and its parts are components. Found by the
  backend's tests running as images on Windows.
- **No header in the backend's sources or tests.** The tests share a module (`mcppide.testsupport`,
  in `testsupport/`) where they included `support.hpp`, and a failed check names its place through
  `std::source_location`. `mcpp-ide-dev check-sources`, run by CI, reports a header, a preprocessor
  branch and a compile-time platform question (`os::is_<platform>`, which an ok image answers false)
  in the backend, its tests and the test support.
- **The mcpp tool window beside Project.** CLion's mcpp tool window is on the left, where VS Code
  shows mcpp, and the first mcpp project opened puts its button on the stripe.
- **Tested as it is run.** The backend is distributed only as an ok image, and CI tests it as one on
  every system: its tests are built as ok images of the system's architecture and run through the
  platform's runner (`backend-openkal`). The native backend is built and tested on Linux x64, and on
  every system in a manual CI run with `native`.

## 0.2.1

mcpp-vscode 1.1.1, mcpp-clion 0.2.1 and mcpp-nvim 0.2.1.

- CLion locates the bundled backend from the plugin descriptor and checks
  protocol compatibility before sending namespaced requests. Backend selection
  and startup failures are recorded in `idea.log`.
- Native settings reject non-finite and out-of-range values without partially
  saving the form. Failed graph queries remain errors and can be retried.
- Debug launches use the actual selected target and custom profile. With mcpp
  2026.11.6.1 or later, a declared `debuggee` runner is selected without guessing
  from device actions; explicit runner choices take precedence. A changed
  target, profile or platform between launch descriptions is refused.
- CLion requests the backend's debug launch and configures LLDB's JIT reader
  for that session. The run-configuration profile field accepts custom names.
- CI covers stable CLion 2026.1.5.2 and 2026.2.4.1 with the same packaged ZIP,
  static compatibility checks and actual editor interactions. The hosted JCEF
  check restores its temporary user-namespace policy after the test.
- CLion 2026.2.4.1 can report an internal GDB variable-read cancellation after
  debug stop, also reproduced without either plugin in a stock CMake project.
  The stable test records its path-free fingerprint and classifies only that
  exact SDK signature after all editor assertions finish. Other IDE errors
  still fail the test.
- Release CI submits the verified CLion ZIP to JetBrains Marketplace using the
  configured token, alongside the verified VSIX sent to Open VSX. Each
  destination has an independent publish job and the JetBrains response is
  retained as a receipt.
- VS Code and CLion check for a missing local `mcpp` only on user actions that
  need the build tool. One notification links to its repository; opening a
  project does not trigger the notice.

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
- **mcpp 2026.11.3.1.** One pack of the target set `openkal` writes the editor packages' tree and
  okb (`--target openkal --mode self-contained --systems all --pkg-format tar,okb`); CI reads
  `runs_on`; the Windows launcher is `bin/mcpp-ide.exe`. The backend is on mcpplibs.os 0.2.8 and
  openkal-platform 0.23.2, ok images included (openkal 0.23: okrun places an image at a base of the
  boot, keeps its data as a prepared copy and starts it from a snapshot of its initialisers; every
  unit, the std module included, is compiled with frame pointers and hardening; mcpp design
  2026-11-03 part 4).
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
