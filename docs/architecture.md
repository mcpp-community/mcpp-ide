# Architecture

## Three layers

| Layer | Holds | Does not hold |
|---|---|---|
| mcpp | the machine contract: event streams, result records, launch descriptors, JSON queries, `--deny` (mcpp docs/50) | any IDE concept |
| mcpp-ide backend (`src/`) | sessions and projects (which mcpp.toml a file belongs to), watching (result records, manifests), jobs (one at a time per project, cancellation, a newer job replacing a waiting one), view models (project, targets and platforms, cache, libraries, graphs), cleanup plans and their danger levels, settings validation and migration, the command table, runtime strings, the self-check, the handshakes with mcpp (`--protocol-version`) and mcppls (`--version`) | parsing mcpp.toml, resolving dependencies |
| adapters (`mide-plugins/`) | declarations generated from `spec/`, the protocol mapped onto native UI, the pages' host, the backend process, finding the backend, workspace trust | any decision |

## The pages

The views and pages a user sees -- the project tree, the libraries and a package's page, the cache,
the settings page, the dependency and module graphs -- are mcpp-vscode's
(`mide-plugins/vscode/src`), as mcpp-vscode 0.9.0 drew them; `registerPages` registers them. They
read projects and the index from disk and run mcpp through the backend's `mcpp/exec` and mcppls
through its `languageService/exec`; the extension starts no other process for either.

The workbench (`webui/`) runs the same modules in a browser host. `webui/src` answers what they ask
of VS Code (the `vscode` module: settings, commands, dialogs, views, webviews) and of Node (`fs`,
`path`, `os`, `crypto`) through a bridge to the IDE; `tools/bundle-webui.mjs` bundles them into one
script. Views are tabs in the manifest's order and names; a webview is an iframe with the page's
own document, its CSP and nonce, and the IDE's colours as the `--vscode-*` variables. The bridge
carries requests through `mcppHostSend`/`mcppHostReceive`, and a synchronous file read as a
`prompt` the host answers. mcpp-clion hosts the workbench in JCEF (`webui/Workbench.kt`).

| Bridge action | What it stands for in VS Code | What mcpp-clion answers (Workbench.kt) |
|---|---|---|
| `rpc` | the backend's method | the backend's method |
| `settings.read`, `settings.write` | the configuration | the plugin's settings store |
| `message`, `input`, `pick`, `saveDialog`, `progress.*` | notifications, input box, quick pick, save dialog, progress | notifications or dialogs, input dialog, list popup, file saver, background task |
| `openFile`, `openText`, `clipboard`, `openExternal` | editors, the clipboard, the browser | editors, the clipboard, the browser |
| `command` | the command | the command table's action |
| `fs.*`, `findFiles` | the file system | the file system |

The parity gates hold the pages to 0.9.0: the manifest's contributions and strings contain 0.9.0's
exactly; every page's document, rendered by the extension's generators, is byte-identical to the
snapshot 0.9.0's generators render (`test/parity`); a Chromium test runs the workbench over a real
backend (`webui/tests`).

## The adapters

| Directory | Plugin | `editor` | Declares | Holds |
|---|---|---|---|---|
| `mide-plugins/vscode` | mcpp-vscode | `vscode` | package.json's contributes, from `mcpp-ide-dev gen vscode` | the pages, tasks, the Testing view, debug configurations, Problems |
| `mide-plugins/clion` | mcpp-clion | `jetbrains` (the IntelliJ platform) | actions and message bundles, from `mcpp-ide-dev gen jetbrains` | run configurations, the test tree, CLion's debugger, the workbench in JCEF |
| `mide-plugins/nvim` | mcpp-nvim | `nvim` | nothing: `:Mcpp` reads the command table from the backend | jobs in a buffer, a terminal for the program, `vim.diagnostic` and the quickfix list, the view models as text, nvim-dap |

`spec/commands.json` says which editors offer a command (`editors`, every editor when absent; a
forward to mcppls's commands only VS Code) and, where an editor is left out, why (`omitted`). The
backend answers each editor's table (`commands/list`) with the rows it offers, their titles in the
session's language and their subcommands, the names Neovim types.

The Neovim plugin is Lua over Neovim's own JSON-RPC client (`vim.lsp.rpc`, the LSP base protocol's
framing). It carries no backend and no web view: a panel's view model is text in a floating window
or a choice in `vim.ui.select`, and a panel with no textual equivalent (the settings, edited in
`setup()`) is left out of its table.

## Language features

mcppls provides every language feature of C++ files; mcpp-ide registers no language service. The
VS Code extension installs mcppls as an extension pack (it can be removed); the CLion plugin depends
on the mcppls CLion plugin; in Neovim mcppls has its own plugin (`xlings install mcppls-nvim`), whose
client's server the Neovim plugin names to the backend. The backend reads `mcppls --version`: 0.0.18 and later refresh after any
build through mcpp's result record, and an older or missing mcppls is reported with what to do.

## A build, end to end

1. The user runs Build. The adapter sends `job/start {kind: "build", root}`.
2. The backend queues it on the project's lane and runs `mcpp build --message-format json` with
   `MCPP_CLIENT`, colour for a terminal, and `MCPP_DENY_EFFECTS` in an untrusted workspace.
3. mcpp's narration becomes `job/output`; each event becomes `job/plan`, `job/progress`,
   `job/diagnostic` or `job/artifact`; the closing envelope arrives in `job/finished`.
4. VS Code shows the narration in a pseudoterminal and the diagnostics in Problems; CLion shows the
   narration in a console and its locations as links; Neovim streams it into `mcpp://output` and
   the diagnostics into the namespace `mcpp`.
5. mcpp writes the result record; the backend's watcher sends `result/changed`; mcppls reads the
   same record and refreshes.

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
bundle and the universal package (`mcpp-ide-<version>-openkal.tar.gz`, a release asset) once, on
Linux, takes okrun from that package, opens both editor packages and the universal package on every
system and starts what they carry for it (`.github/scripts/check-package.sh`).

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
