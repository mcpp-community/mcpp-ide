---
name: mcpp-ide-development
description: Use when setting up a development environment for mcpp-ide, building or testing the backend or an editor plugin, changing the protocol, spec/, fixtures or strings, bumping a pinned tool, or preparing a release (a series vX.Y.0 or a plugin patch).
---

# Developing mcpp-ide

## 1. Environment

The pins live in [.github/versions.env](../../../.github/versions.env), which CI reads. A local
setup uses the same versions:

| Tool | Version | For | Install |
|---|---|---|---|
| xlings | `XLINGS_VERSION` | mcpp, toolchains, gradle | [xlings](https://github.com/d2learn/xlings) |
| mcpp | `.xlings.json` (= `MCPP_VERSION`) | the backend; its toolchain comes from `mcpp.toml` | `xlings install` in the repository |
| Node.js | 22 | the VS Code extension, the web UI tests | any Node 22 |
| VS Code | `VSCODE_VERSION` (the oldest supported) | the extension's e2e tests | fetched by `@vscode/test-electron` |
| Gradle and its JDK | xlings's | the CLion plugin | `xlings install gradle` |
| Neovim | 0.10+ | the Neovim plugin | system package |

Check the environment with `mcpp build && mcpp run mcpp-ide -- self-check`.

## 2. Build and test, per component

Run what a change reaches ([.github/components.tsv](../../../.github/components.tsv)). A backend
change reaches every plugin, because each plugin is tested over the backend.

| Component | Paths | Commands |
|---|---|---|
| backend | `src/`, `protocol/`, `spec/`, `webui/`, `tests/`, `testsupport/`, `mcpp.toml` | `mcpp build`, `mcpp test`, `mcpp run mcpp-ide-dev -- check-sources`, `mcpp run mcpp-ide-dev -- check-i18n` |
| vscode | `mide-plugins/vscode/` | `npm ci && npm test`; `npm run test:parity`, `npm run test:webui`, `npm run test:e2e`; `npm run package` |
| clion | `mide-plugins/clion/` | `gradle test buildPlugin verifyPlugin` |
| nvim | `mide-plugins/nvim/` | `nvim --headless --clean -l mide-plugins/nvim/tests/smoke.lua` |
| CI | `.github/` | `bash .github/scripts/test_changes.sh`, `bash .github/scripts/release-plan.sh` |

## 3. Common changes

- **A protocol method or notification:** change `protocol/protocol.schema.json` together with the
  backend and every plugin that uses it. The backend's tests check its answers against the schema.
- **A setting, command or editor string:** edit `spec/` (`settings.json`, `commands.json`,
  `i18n/*.json`), then run `mcpp run mcpp-ide-dev -- gen vscode` and commit what it rewrites in
  `mide-plugins/vscode`. CLion's declarations are generated at build time. Neovim reads the table at
  run time. A command that an editor does not offer states why (`omitted`).
- **A runtime string:** pass it through `t(...)` and add its Chinese entry to
  `spec/i18n/runtime.zh-cn.json`; `check-i18n` enforces this.
- **What mcpp reports:** run `mcpp run mcpp-ide-dev -- capture` against a real mcpp and review the
  diff in `protocol/fixtures`. Update `protocol/mcpp-contract.schema.json` if the contract grew.
  The mock mcpp and every plugin's tests replay these fixtures.
- **A feature of a newer mcpp or mcppls:** enable it from what mcpp states (`features`). An earlier
  release is described by a table frozen at that release, not by comparing versions.
- **A pinned tool:** change `.github/versions.env`, `.xlings.json` and `mcpp = ">=..."` in
  `mcpp.toml` together, and only to versions that are already released and indexed.

## 4. Diagnosing

- `mcpp-ide self-check --json` reports the backend, mcpp and its contract, mcppls, xlings and trust.
- The IDE report (**mcpp: Export IDE Report**, `:Mcpp exportReport`) collects the projects' mcpp,
  mcppls's status, the settings in effect, recent jobs and the backend's log.
- `mcpp.backend.logLevel` controls how much the backend writes to the editor's log.

## 5. Release

The rules are in AGENTS.md §4 and [release-plan.sh](../../../.github/scripts/release-plan.sh).

1. Decide the kind. Any backend path changed since `vX.Y.0` means a series release `vX.Y+1.0` (or
   `vX+1.0.0`). Otherwise it is a plugin patch, `<vscode|clion|nvim>-vX.Y.Z+1`.
2. On `release/<version>`, bump the version statements (all six for a series, only the plugin's
   own for a patch) and add the CHANGELOG.md section under its series, listing each component's
   version. Then run `bash .github/scripts/release-plan.sh <tag>`.
3. Open a pull request titled `X.Y.Z: <sentence>` and merge it after `ci` passes.
4. Tag the merge commit and push the tag. `release.yml` runs CI, publishes the GitHub release with
   `SHA256SUMS`, then submits the same VSIX to Open VSX and the same ZIP to JetBrains Marketplace.
   The Visual Studio Marketplace upload is manual. The secrets are `OPENVSX_TOKEN` and
   `JETBRAINS_TOKEN`; Marketplace availability follows JetBrains review.
5. Watch the run until every job passes, then check that the release assets install
   (`mcpp-ide self-check` on a project).
