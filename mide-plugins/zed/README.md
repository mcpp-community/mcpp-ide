# mcpp-zed

mcpp-ide in [Zed](https://zed.dev): completion, hover and diagnostics of `mcpp.toml`, from the same
backend as VS Code, CLion and Neovim. Zed's id for it is `mcpp-ide-language-server`; it is listed as
**mcpp IDE**.

## Install

Zed's extensions page (command palette → **zed: extensions**): search for **mcpp IDE** and install
it, with Zed's **TOML** extension, which gives Zed the language `mcpp.toml` is written in.

Until it is in Zed's registry, install it from a checkout: command palette → **zed: install dev
extension** → `mide-plugins/zed`. Zed compiles it, so Rust has to be installed through rustup.

## The backend

The backend is the one of the extension's own series, from the first of these:

1. the command `lsp.mcpp-ide.binary` names in Zed's settings. Zed starts it with the `arguments`
   given there and nothing else, so they include `serve`:

   ```json
   { "lsp": { "mcpp-ide": { "binary": { "path": "/path/to/mcpp-ide", "arguments": ["serve"] } } } }
   ```

2. the release's bundle, `mcpp-ide-X.Y.0-openkal-bundle-all.tar.gz` of the tag `vX.Y.0`, which the
   extension downloads once into the work directory Zed gives it (from GitHub, else from the GitCode
   mirror `xlings-res/mcpp-ide`) and starts as this system's runner and this architecture's image.
   The bundle has them for Linux x86_64 and aarch64, macOS aarch64 and Windows x86_64.

A `mcpp-ide` on PATH is not started. The extension cannot read the series a backend answers, so it
could not refuse a backend of another series as the other plugins do. To use the xlings package,
name it in `lsp.mcpp-ide.binary`.

Zed starts the backend with a worktree's first TOML file, and only in a worktree it trusts. The
backend finds the worktree's mcpp projects itself and reads `mcpp.toml` and `build.mcpp` only, so
in a worktree without one it answers nothing. The first TOML file after an install downloads the
bundle, about 13 MB.

## Settings

The backend's settings ([spec/settings.json](../../spec/settings.json)) go under `lsp.mcpp-ide.settings`
and reach it when it starts:

```json
{ "lsp": { "mcpp-ide": { "settings": { "mcpp.path": "/opt/mcpp/bin/mcpp", "mcpp.toml.diagnostics.unknownKey": "error" } } } }
```

## What it does, and what it does not yet

| In Zed | |
|---|---|
| `mcpp.toml` | completion of sections, keys and values, of a dependency's features; hover; diagnostics, unknown keys and features included |
| `build.mcpp` | as C++, with mcppls: `"file_types": { "C++": ["mcpp"] }` in Zed's settings |
| C and C++ | mcppls's own extension, **C++ Modules Language Server** (`mcppls-language-server`) |
| build, run, test, debug, the views | not yet: Zed's tasks and its debug locator follow in pull requests of their own ([design record](../../.agents/docs/2026-11-21-editor-surfaces-and-feature-matrix.md), "Zed, tier B") |

## Tests

`cargo test` covers which backend is started and how: the settings, the series' release and its
mirror, each platform's runner and image, the errors and the initialization options. CI builds the
crate for `wasm32-wasip2` with the Rust Zed's registry uses (`ZED_RUST_VERSION` in
[versions.env](../../.github/versions.env)) and runs what the registry checks of the manifest and
the license (`.github/scripts/check-zed-extension.sh`).

## Zed's extension registry

[zed-industries/extensions](https://github.com/zed-industries/extensions) builds this directory at
the commit a pull request there names and publishes it under `mcpp-ide-language-server`. A tag
`zed-vX.Y.Z` here marks that commit; the GitHub release carries no file of its own, and the
extension downloads the backend of its series from `vX.Y.0`.

What the registry requires ([prerequisites](https://zed.dev/docs/extensions/publishing/prerequisites),
[publishing guide](https://zed.dev/docs/extensions/publishing/publishing-guide), its CI):

| Rule | Here |
|---|---|
| An id of lowercase letters, digits and `-`, without `zed` or `extension`; an extension that provides only a language server ends in `-language-server` or `-lsp`; an id never changes once published | `mcpp-ide-language-server` |
| An accepted license in the extension's own directory; the repository's root does not count | `LICENSE`, Apache-2.0 |
| The server is downloaded or found, not bundled; nothing is read or written outside the work directory | the release's bundle, downloaded into the work directory |
| `version` in its `extensions.toml` is `extension.toml`'s at that commit, and only rises | the series' statement (AGENTS.md §4) |
| It builds with the registry's Rust 1.90 for `wasm32-wasip2` | CI's Zed job |
| Tested by hand in Zed at the submitted commit; text in English | |

The registry's [AI policy](https://github.com/zed-industries/extensions/blob/main/AI_POLICY.md)
admits no autonomous agent: a person opens the pull request, writes its description and answers its
review. A pull request adds or updates one extension, and one left three weeks without an answer to
a review is closed.

A version reaches Zed in four steps:

1. The release `vX.Y.0` of its series exists, and the commit is on `main`, tagged `zed-vX.Y.Z`.
2. It is installed as a dev extension in Zed at that commit and opened on an mcpp project: the
   bundle is downloaded and `mcpp.toml` has its diagnostics.
3. From a fork of zed-industries/extensions under a personal account (not an organization):

   ```bash
   git submodule add https://github.com/mcpp-community/mcpp-ide.git extensions/mcpp-ide-language-server
   git -C extensions/mcpp-ide-language-server checkout zed-vX.Y.Z
   ```

   and in `extensions.toml`:

   ```toml
   [mcpp-ide-language-server]
   submodule = "extensions/mcpp-ide-language-server"
   path = "mide-plugins/zed"
   version = "X.Y.Z"
   ```

   then `pnpm sort-extensions`, which orders `extensions.toml` and `.gitmodules`.
4. The pull request, titled `Add mcpp-ide-language-server` (later `Update
   mcpp-ide-language-server to X.Y.Z`), with the registry's checklist confirmed. An update moves the
   submodule to the new tag and changes the version.
