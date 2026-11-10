# Architecture

## Three layers

| Layer | Holds | Does not hold |
|---|---|---|
| mcpp | the machine contract: event streams, result records, launch descriptors, JSON queries, `--deny` (mcpp docs/50) | any IDE concept |
| mcpp-ide backend (`src/`) | sessions and projects (which mcpp.toml a file belongs to), watching (result records, manifests), jobs (one at a time per project, cancellation, a newer job replacing a waiting one), view models (project, targets and platforms, cache, libraries, graphs), cleanup plans and their danger levels, settings validation and migration, the command table, runtime strings, the self-check, the handshakes with mcpp (`--protocol-version`) and mcppls (`--version`), the rules layer of mcpp.toml and build.mcpp as an LSP server ([rules.md](rules.md)) | a manifest's meaning, resolving dependencies |
| adapters (`mide-plugins/`) | declarations generated from `spec/`, the protocol mapped onto native UI, the pages' host, the backend process, finding the backend, workspace trust | any decision |

## The pages

The views and pages of VS Code -- the project tree, the libraries and a package's page, the cache,
the settings page, the dependency and module graphs -- are mcpp-vscode's (`mide-plugins/vscode/src`),
as mcpp-vscode 0.9.0 drew them; `registerPages` registers them. They read projects and the index
from disk and run mcpp through the backend's `mcpp-ide/mcpp/exec` and mcppls through its
`mcpp-ide/languageService/exec`; the extension starts no other process for either. The parity gates
hold them to 0.9.0: the manifest's contributions and strings contain 0.9.0's exactly, and every
page's document, rendered by the extension's generators, is byte-identical to the snapshot 0.9.0's
generators render (`test/parity`).

CLion has the same information in its own components (design 2026-10-30 part 2 §9): the "mcpp"
tool window's Project, Libraries and Cache tabs are native trees and lists over the backend's view
models (`projects/list`, `project/summary`, `project/mcpp`, `library/search`, `cache/inventory`),
a package's page is HTML in the Libraries tab, the cleanups are the command table's with their
confirmations, and Settings | Tools | mcpp is a form generated from `settings/registry`. A graph is
the one page in JCEF: `webui/graph.html` draws the backend's `graph/packages` or `graph/modules`
with mermaid and asks the IDE to open a module's source; a Chromium test runs it over a real
backend (`webui/tests`). The strings are the backend's (`i18n/bundle`), the ones VS Code shows.

## The adapters

| Directory | Plugin | `editor` | Declares | Holds |
|---|---|---|---|---|
| `mide-plugins/vscode` | mcpp-vscode | `vscode` | package.json's contributes, from `mcpp-ide-dev gen vscode` | the pages, tasks, the Testing view, debug configurations, Problems |
| `mide-plugins/clion` | mcpp-clion | `jetbrains` (the IntelliJ platform) | actions and message bundles, from `mcpp-ide-dev gen jetbrains` | run configurations, the test tree, CLion's debugger, the tool window's native views, the settings form, the graph page in JCEF |
| `mide-plugins/nvim` | mcpp-nvim | `nvim` | nothing: `:Mcpp` reads the command table from the backend | jobs in a buffer, a terminal for the program, `vim.diagnostic` and the quickfix list, the view models as text, nvim-dap |

`spec/commands.json` says which editors offer a command (`editors`, every editor when absent; a
forward to mcppls's commands only VS Code) and, where an editor is left out, why (`omitted`). The
backend answers each editor's table (`commands/list`) with the rows it offers, their titles in the
session's language and their subcommands, the names Neovim types.

The Neovim plugin is Lua over Neovim's own LSP client (`vim.lsp`), attached to mcpp.toml and
build.mcpp buffers. It carries no backend and no web view: a panel's view model is text in a floating window
or a choice in `vim.ui.select`, and a panel with no textual equivalent (the settings, edited in
`setup()`) is left out of its table.

## Language features

mcppls provides every language feature of C++ files; mcpp-ide registers no C++ language service.
The VS Code extension installs mcppls as an extension pack (it can be removed); the CLion plugin
depends on the mcppls CLion plugin; in Neovim mcppls has its own plugin (`xlings install
mcppls-nvim`), whose client's server the Neovim plugin names to the backend. The backend reads
`mcppls --version`: 0.0.18 and later refresh after any build through mcpp's result record, and an
older or missing mcppls is reported with what to do.

mcpp.toml and build.mcpp are the backend's: the session is an LSP server (protocol 2), and its rules
layer answers their completion, hover, jumps and diagnostics in every editor
([rules.md](rules.md)). In build.mcpp, which mcppls compiles as a build program, the backend leaves
mcppls what a compiler answers better.

## A build, end to end

1. The user runs Build. The adapter sends `mcpp-ide/job/start {kind: "build", root}`.
2. The backend queues it on the project's lane and runs `mcpp build --message-format json` with
   `MCPP_CLIENT`, colour for a terminal, and `MCPP_DENY_EFFECTS` in an untrusted workspace.
3. mcpp's narration becomes `job/output`; each event becomes `job/plan`, `job/progress`,
   `job/diagnostic` or `job/artifact`; the closing envelope arrives in `job/finished`.
4. VS Code shows the narration in a pseudoterminal and the diagnostics in Problems; CLion shows the
   narration in a console and its locations as links; Neovim streams it into `mcpp://output` and
   the diagnostics into the namespace `mcpp`.
5. mcpp writes the result record; the backend's watcher sends `result/changed`; mcppls reads the
   same record and refreshes.

## A test run, end to end

The editors show tests as `mcpp test` states them, by the mcpp test spec (mcpp docs/specs/test.md). mcpp
builds and runs the programs and states their cases and results; a framework is whatever library a
program is written with, and the backend knows none by name. What a framework offers beyond the base,
it states of itself (`mcpp test --guide`, B6).

1. `test/list` runs `mcpp test --list` and answers each program with its files, its `main` and its parts.
   A protocol program of several files (`[test] programs = "directory"`) runs as one process per file,
   `<program>/<stem>`. mcpp states the parts with `test-parts`; before it, the backend derives them as
   mcpp 2026.11.9.1 forms them. The programs are kept per project.
2. `test/cases` answers a program's cases as the program lists them (`mcpp test --cases`, B3): one
   listing of the project, kept until the project changes.
3. A test job runs `mcpp test --message-format json` with one set of arguments for every test command
   (the profile, the target, `mcpp.task.testArgs`). Each record becomes `test/result`, with the program
   it belongs to, the part it is and its cases, each status mapped (`xfail` passes, `xpass` fails,
   `invalid` is errored); a case carries its checks (O3) and its own output (O8). With
   `test-case-stream` each case is sent as it begins and ends (`test/caseEvent`). A test job runs in a
   lane of its own: two tests, or a test and a build, do not wait for each other.
4. Where a framework states a test session (`test-session`, O9) and mcpp gives launches
   (`test-launch-records`, B5), `test/runCases` runs the cases through it: the session is started once
   per project, speaks JSON-RPC on its standard streams, answers a run at once and states its build,
   its cases and its end as notifications, which become the job's `test/caseEvent` and `job/finished`.
   The session ends with its input, so it ends with the backend. `mcpp.test.session = "off"` runs
   every case through `mcpp test`.
5. VS Code marks the Testing view's items, one message per failed check (a diff where both sides are
   stated), and ends a case's line with its duration, its failed checks and its metrics. CLion fills an
   ID-based test tree: a suite per program and per file, a comparison failure when both sides are
   stated. Neovim writes `mcpp://tests`, the quickfix list and the signs.
6. `test/insights` reads the last runs mcpp keeps (`target/mtest`, or `[test] store`; 20 by default)
   and answers the last run, its failures and since when, the slowest and the flaky cases, each
   metric against its bound, and the schedule with its critical path. VS Code and CLion draw it with
   one page (`webui/tests.html`); Neovim prints it.

Every step reads what the project's mcpp states and nothing more: an older mcpp gives programs and
their results, and a view that needs a later feature is left out (`capabilities/explain`).

The backend's own tests are mtest's (mtest docs/layout.md): `tests/unit`, `tests/e2e` (the wire and a
session over the mock mcpp), `tests/cts` (the contracts against their schemas, the rules layer against
its goldens) and `tests/bench` (the latency of a keystroke in a large manifest, of reading a large test
file, of the insights over a full window), each directory one program. CI reports each run with
`mtest report` in the job's summary.

## Distribution

The backend is one program for every editor, and one file for every system: an ok image per
architecture (`mcpp build --target <arch>-openkal`, openkal-platform's toolchain and C library),
packed as one ok-image bundle (`--pkg-format okb`). Each
editor package carries that bundle and the runner that starts it on each system:

| Layout in the package | Holds | Started as |
|---|---|---|
| `backend/mcpp-ide.okb` | the x86_64 and aarch64 images | |
| `backend/okrun/<os>-<arch>/okrun[.exe]` | okrun for `linux-x86_64`, `linux-aarch64`, `macos-aarch64`, `windows-x86_64`, from the universal self-contained package (`--mode self-contained --systems all`: each okrun built over the platform's hosted arrangement); one command packs both, `mcpp pack mcpp-ide --target openkal --mode self-contained --systems all --pkg-format tar,okb`, and the package's `runs_on.self` names the systems it runs on | `okrun mcpp-ide.okb serve`: the okrun carried for this system, else okrun on PATH |

Before it, a configured `mcpp.ide.backendPath` (a program or an `.okb`) is used; after it, an
installed `mcpp-ide` (xlings, whose command runs okrun on its bundle) or one on PATH. The Neovim
plugin carries no package of the backend: it starts setup()'s `backend`, else `mcpp-ide` on PATH,
and `xlings install mcpp-ide-nvim` installs it from the release's source tree, with the backend's
package as its dependency. CI packs the
bundle and the target set's tree (`mcpp-ide-<version>-openkal-bundle-all.tar.gz`, a release asset) in
one pack on Linux, takes okrun from that tree, opens both editor packages and the tree on every
system and starts what they carry for it (`.github/scripts/check-package.sh`).

The CLion adapter obtains its installation directory from the IDE's plugin descriptor. Its
class loader's `CodeSource` URL is not required. The session accepts protocol 2 before sending
`mcpp-ide/i18n/bundle` and `mcpp-ide/commands/list`. Backend selection and startup failures are
recorded in `idea.log`; RPC startup failures include the method and error code. The stable
CLion runtime checks install one packaged plugin ZIP in both stable IDE families and exercise
the complete startup chain in each IDE process. Functional checks cover build, run, test,
native debug launch, error recovery, manifest completion and view selection. The debugger
adapter starts the native Cidr process from the session starter before returning it.

The debug request first obtains the program's actual launch. With mcpp
2026.11.6.1 or later, an available `debuggee` named runner is selected on that
launch's target and profile; an explicit runner parameter takes precedence.
The selected launch is checked again for the same target, profile and platform.
CLion requests this backend debug configuration and enables LLDB's JIT reader
within the session when starting a runner. Custom run-configuration profiles
are forwarded as `--profile`.

## Generated declarations

`spec/` is the source of everything the editors declare statically. `mcpp-ide-dev gen vscode` writes
the extension's package.json (commands, keybindings, menus, configuration), its message bundles and
its settings registry in place, in `mide-plugins/vscode` (CI regenerates them and expects no change); `mcpp-ide-dev gen jetbrains` writes the CLion plugin's actions and
message bundles into `mide-plugins/clion/build`. The backend embeds the same files (build.mcpp), so
the commands an editor registers at run time are the ones it declared; which rows an editor offers
is one rule (`spec::offered_in`) for both. The Neovim plugin declares nothing and offers what the
backend's table holds.

## Fixtures

`protocol/fixtures` holds mcpp's real output, captured by `mcpp-ide-dev capture`. The backend's tests
check each fixture against `protocol/mcpp-contract.schema.json`, replay them through the mock mcpp,
and check the backend's answers against `protocol/protocol.schema.json`; the adapters' tests run on
the same mock (the Neovim plugin's headless suite also over a real mcpp).
