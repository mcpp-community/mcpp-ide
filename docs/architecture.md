# Architecture

## Three layers

| Layer | Holds | Does not hold |
|---|---|---|
| mcpp | the machine contract: event streams, result records, launch descriptors, JSON queries, `--deny` (mcpp docs/50) | any IDE concept |
| mcpp-ide backend (`src/`) | sessions and projects (which mcpp.toml a file belongs to), watching (result records, manifests), jobs (one at a time per project, cancellation, a newer job replacing a waiting one), view models (project, targets and platforms, cache, libraries, graphs), cleanup plans and their danger levels, settings validation and migration, the command table, runtime strings, the self-check, the handshakes with mcpp (`--protocol-version`) and mcppls (`--version`) | parsing mcpp.toml, resolving dependencies |
| adapters (`vscode/`, `jetbrains/`) | declarations generated from `spec/`, the protocol mapped onto native UI, the panels' host, the backend process, finding the backend, workspace trust | any decision |

The panels (`webui/`) are one set of classic scripts that both editors load: a VS Code webview and a
JCEF page. A panel speaks the backend's protocol through its host and asks the host only for what
the editor does (open a file, confirm, store a setting).

## Language features

mcppls provides every language feature of C++ files; mcpp-ide registers no language service. The
VS Code extension installs mcppls as an extension pack (it can be removed); the CLion plugin depends
on the mcppls CLion plugin. The backend reads `mcppls --version`: 0.0.18 and later refresh after any
build through mcpp's result record, and an older or missing mcppls is reported with what to do.

## A build, end to end

1. The user runs Build. The adapter sends `job/start {kind: "build", root}`.
2. The backend queues it on the project's lane and runs `mcpp build --message-format json` with
   `MCPP_CLIENT`, colour for a terminal, and `MCPP_DENY_EFFECTS` in an untrusted workspace.
3. mcpp's narration becomes `job/output`; each event becomes `job/plan`, `job/progress`,
   `job/diagnostic` or `job/artifact`; the closing envelope arrives in `job/finished`.
4. VS Code shows the narration in a pseudoterminal and the diagnostics in Problems; CLion shows the
   narration in a console and its locations as links.
5. mcpp writes the result record; the backend's watcher sends `result/changed`; mcppls reads the
   same record and refreshes.

## Distribution

The backend is one program for every editor, and one file for every system: an ok image per
architecture (`mcpp build --target <arch>-openkal`, openkal-platform's toolchain and C library),
packed as one ok-image bundle (`mcpp pack mcpp-ide --target x86_64-openkal --pkg-format okb`). Each
editor package carries that bundle and the runner that starts it on each system:

| Layout in the package | Holds | Started as |
|---|---|---|
| `backend/mcpp-ide.okb` | the x86_64 and aarch64 images | |
| `backend/okrun/<os>-<arch>/okrun[.exe]` | okrun for `linux-x86_64`, `linux-aarch64`, `macos-aarch64`, `windows-x86_64`: the universal self-contained packages (`mcpp pack --mode self-contained`) packed on Linux (`--systems linux,windows`) and on macOS, joined by `mcpp pack --merge` | `okrun mcpp-ide.okb serve`: the okrun carried for this system, else okrun on PATH |

Before it, a configured `mcpp.ide.backendPath` (a program or an `.okb`) is used; after it, an
installed `mcpp-ide` (xlings, whose command runs okrun on its bundle) or one on PATH. CI builds the
bundle once, joins the self-contained packages (`mcpp-ide-<version>-openkal.tar.gz`, a release asset),
takes okrun from the joined package, opens both editor packages and the joined package on every
system and starts what they carry for it (`.github/scripts/check-package.sh`).

## Generated declarations

`spec/` is the source of everything the editors declare statically. `mcpp-ide-dev gen vscode` writes
the extension's package.json (commands, keybindings, menus, configuration) and its message bundles;
`mcpp-ide-dev gen jetbrains` writes the plugin's actions and message bundles. The backend embeds the
same files (build.mcpp), so the commands an editor registers at run time are the ones it declared.

## Fixtures

`protocol/fixtures` holds mcpp's real output, captured by `mcpp-ide-dev capture`. The backend's tests
check each fixture against `protocol/mcpp-contract.schema.json`, replay them through the mock mcpp,
and check the backend's answers against `protocol/protocol.schema.json`; the adapters' tests run on
the same mock.
