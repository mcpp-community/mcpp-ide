# AGENTS.md

mcpp-ide is the IDE layer of [mcpp](https://github.com/mcpp-community/mcpp): one backend (`mcpp-ide serve`,
C++23 modules) and the editor plugins that host it (`mide-plugins/`: VS Code, CLion, Neovim, Zed). This file
gives the rules in their shortest form and links to the full statement, which holds where they differ.

## 1. Where a fact lives

| Path | States |
|---|---|
| [docs/architecture.md](docs/architecture.md) | the layers, the adapters, a request end to end, distribution |
| [docs/versions.md](docs/versions.md) | series and tags, the project's mcpp and what each of its features adds |
| [protocol/](protocol) | the backend's protocol, the part of mcpp's contract it reads, mcpp's real output (`fixtures/`) |
| [spec/](spec) | settings, commands, strings: the source of every editor's declarations |
| [.agents/docs/](.agents/docs) | design records, `YYYY-MM-DD-<subject>.md`, each opening with its status |
| [.agents/skills/](.agents/skills) | procedures: [contributing](.agents/skills/mcpp-ide-contributing/SKILL.md) (issues, pull requests, agents), [development](.agents/skills/mcpp-ide-development/SKILL.md) (environment, tests, release) |
| [agents/](agents/README.md) | for agents that *use* mcpp-ide: the [mcpp-ide skill](agents/skills/mcpp-ide/SKILL.md) |
| `mide-plugins/<editor>/README.md` | one plugin: installing it, its settings, what it maps onto |
| [CHANGELOG.md](CHANGELOG.md) | by series; release.yml publishes it as the release notes |

## 2. Who owns what

| Party | Owns | Never |
|---|---|---|
| mcpp | the machine contract: events, result records, launches, JSON queries, `features` | an IDE concept |
| backend (`src/`) | every decision: projects, jobs and lanes, view models, cleanup plans, settings, the command table, strings, the rules of mcpp.toml and build.mcpp | a manifest's meaning, resolving dependencies |
| plugins (`mide-plugins/`) | the protocol mapped onto the editor's native UI, the backend process, workspace trust | any decision |
| mcppls | every language feature of C++ files | |

- **One answer everywhere.** What two editors would both decide is the backend's. A decision made
  in a plugin is a defect, even a correct one.
- **Only mcpp's contract.** A fact mcpp does not state is requested from mcpp (an issue there). It is
  never derived by parsing mcpp's files or its human-readable output.
- **Capabilities, not versions.** A feature is enabled by what the project's mcpp (or mcppls) states.
  An older release is described by a table frozen at that release. Versions are never compared.
- **Native in each editor.** A plugin uses its editor's own components. VS Code's pages stay
  byte-identical to mcpp-vscode 0.9.0 (`test/parity`).
- **Doing less is never silent.** A missing capability is reported with the party responsible and
  the remedy (`capabilities/explain`, `self-check`).

## 3. Before a change: triage

1. **Whose problem is it?** Reproduce it without the editor: `mcpp-ide self-check --json`, the
   protocol over `mcpp-ide serve`, the mock mcpp. Wrong output from mcpp goes to mcpp, a C++ language
   feature to mcppls, an editor API to that editor's upstream. Report it there with the reproduction.
2. **Backend or plugin?** If another editor would need the same change, it belongs to the backend.
   A plugin changes only for how an answer is shown or for its host's API.
3. **Fix or workaround?** Fix the cause in the party that owns it. A problem that is not this
   project's gets no workaround by default. A workaround that must ship anyway (a blocked release,
   an upstream fix that will not arrive in time) states in the code and in the pull request: the
   cause, the upstream issue, why it cannot wait, and when it will be removed; it stays narrow.

## 4. Versions ([release-plan.sh](.github/scripts/release-plan.sh))

- A version is `X.Y.Z`. The series `X.Y` is shared by the backend and every plugin; `Z` is one
  plugin's own patch. The backend is always `X.Y.0`: every package carries it, so a change to any
  backend path ([components.tsv](.github/components.tsv)) begins a new series.
- Seven statements: `mcpp.toml`, `src/server.cppm` (`kVersion`, `kSeries`), `vscode/package.json`,
  `clion/gradle.properties` (`pluginVersion`), `nvim/lua/mcpp/session.lua` (`M.version`),
  `zed/extension.toml` (with its `Cargo.toml` and `Cargo.lock`). A series changes all of them in one commit, a patch
  only its plugin's; CI checks that they agree.
- Tags: `vX.Y.0` releases every component. `vscode-vX.Y.Z`, `clion-vX.Y.Z`, `nvim-vX.Y.Z` and
  `zed-vX.Y.Z` release one plugin's patch, and are refused if the backend changed after `vX.Y.0`.
  Zed's registry builds a tagged commit through a pull request a person opens there
  (`mide-plugins/zed/README.md`).
- Pins name only released versions: [versions.env](.github/versions.env) (CI), `.xlings.json`, `mcpp = ">=..."`.

## 5. Build and test

```bash
mcpp build && mcpp test                                      # backend (mtest: unit, e2e, cts, bench)
mcpp run mcpp-ide-dev -- check-sources && mcpp run mcpp-ide-dev -- check-i18n
(cd mide-plugins/vscode && npm ci && npm test)               # extension, parity gates included
(cd mide-plugins/clion && gradle buildPlugin verifyPlugin)   # CLion plugin
nvim --headless --clean -l mide-plugins/nvim/tests/smoke.lua # Neovim plugin
(cd mide-plugins/zed && cargo test)                          # Zed extension
```

- Edit `spec/`, never what is generated from it. `mcpp-ide-dev gen vscode` rewrites the extension's
  manifest, bundles and registry, and CI expects no diff afterwards.
- A change in what mcpp reports begins with `mcpp-ide-dev capture` and the diff in `protocol/fixtures`,
  which the mock mcpp replays; CI runs the components a change reaches (`components.tsv`).

## 6. Code

- Backend: C++23 modules and `import std;`, with no header, no `#include` and no `#if`. The system is
  reached only through `mcpplibs.os` (`os::host()`, never `os::is_<os>`, which an ok image answers
  false). `check-sources` enforces all of this.
- CLion: public IntelliJ API only (the verifier reports internal use). VS Code: the oldest
  `engines.vscode` (`VSCODE_VERSION`). Neovim: 0.10 and later, over `vim.lsp`. Zed:
  `zed_extension_api` for `wasm32-wasip2` and the rules of Zed's registry; its few messages, shown
  before a backend answers, are English, as the registry asks.
- User-facing strings belong to the backend (`spec/i18n`), and English and Chinese change together.
- Follow the neighbouring code and change only what the task needs. A comment says why, not what.

## 7. Change workflow

- Follow the [contributing skill](.agents/skills/mcpp-ide-contributing/SKILL.md): an issue from a
  template, a branch `<type>/<subject>` from `main`, a pull request, passing `ci`, review, a squash
  merge. Nothing is pushed directly to `main`; an agent asks before any outward action.
- Commits and pull requests are English and declarative, `<type>(<component>): <sentence>` or
  `X.Y.Z: <sentence>`, with no attribution trailer. Documentation follows mcpp's
  [docs style](https://github.com/mcpp-community/mcpp/blob/main/.agents/skills/mcpp-docs-style/SKILL.md),
  and README.md and README.zh-CN.md stay in step.
- One pull request does one thing. A change in behaviour updates the protocol schema, the affected
  docs and CHANGELOG.md in the same pull request; a decision worth keeping goes to `.agents/docs/`.
