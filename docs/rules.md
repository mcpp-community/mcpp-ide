# The rules layer: mcpp.toml and build.mcpp

The backend reads the two files mcpp defines beside C++ sources, `mcpp.toml` and `build.mcpp`, and
answers completion, hover, the jumps of `mcpp.toml` and the diagnostics of both (mcpp design
2026-10-30 part 2 §8). The answers are text analysis: nothing is run, so they hold in an untrusted
workspace too. Whether a manifest is valid is mcpp's to say (`mcpp-ide/manifest/check`, located
diagnostics); the rules layer says what the text in the editor already shows.

## The connection

`mcpp-ide serve` is an LSP server (protocol 2). `initialize` has LSP's shape, with mcpp-ide's
parameters in `initializationOptions`; the rules layer answers LSP's `textDocument/*` methods, and
mcpp-ide's own methods and notifications are under `mcpp-ide/`. Every editor connects to the one
backend process and so to one project state:

| Editor | Client | The rules layer reaches the editor as |
|---|---|---|
| VS Code | VS Code's language client (bundled into `dist/vendor`) | the client's completion, hover, definition and the `mcpp` diagnostic collection |
| CLion | the session's connection (the platform's LSP client starts a server of its own) | a completion contributor, an external annotator, a documentation provider, a go-to-declaration handler |
| Neovim | Neovim's LSP client (`vim.lsp`) | the client attached to `mcpp.toml` and `build.mcpp` buffers |

The documents are synchronised whole (`didOpen`, then `didChange` with each new text). A position is
a 0-based line and a UTF-16 column. Completion, hover, definition and the outline are answered in
the order the messages arrive, so an answer sees the document the notifications before it left.

## mcpp.toml

| Cursor | Suggestions |
|---|---|
| On a table header, or before the first one | every table of the snapshot (a table written only under a selector, `[target.'cfg(...)'.abi]`, with its selector to fill in), and the forms that name something (`[targets.<name>]`, `[dependencies.<namespace>]`, `[features.<name>]`, `[target.'cfg(...)'.build]`, ...) |
| Inside `[[` | nothing: a suggestion would turn an array of tables into a table |
| On a key of a table with a closed vocabulary | its keys, less those already written in the table's group; a key's detail names its type, values and default |
| On a key of a dependency table | the five dependency forms |
| On a key of `[features]`, `[generated_files]`, `[capabilities]`, `[xlings.workspace]`, `[tools.overrides]` | that table's forms |
| Inside an inline table, after `{` or `,` (1.6.0) | the keys of the table of its name, less those the inline table has on either side of the cursor: `app = { ... }` under `[targets]` lists what `[targets.app]` does, and is not an unknown key of `[targets]`. A dependency's inline table is its specification, whose keys the snapshot does not list, and gets nothing |
| On an enum key's value | its values (inside a string only the content is replaced); a key of an inline table or a dotted key has its own table's |
| Inside `features = [...]` of a dependency given by `path` or `workspace = true` (1.6.0) | the features its own manifest declares and the edge does not name yet, each with whether it is a default and what it implies |
| On a dependency's version, with `mcpp.toml.indexCompletion` on and the workspace trusted | the versions mcpp's catalogue (`mcpp search --format json`) lists for that package, newest first; the catalogue is read once in the background (the answer is incomplete until then) and kept for ten minutes |

A table is read as the snapshot's section its name denotes: the whole name when the snapshot has
it; `[workspace.<table>]` as the table it mirrors; a platform's table under a target selector
(`[target.<sel>.link]`) as the platform's (`[platform.link]`); `[target.<sel>.<table>]` as
`<table>`; a named entry (`[targets.app]`, `[platform.build.'cfg(...)']`) as the longest leading
run that names a section.

Hover describes a table (its plane, its keys), a key (type, values, default, the release that added
it, its note) and an enum value. Definition jumps from `workspace = true` to the entry of
`[workspace.dependencies]`, from an element of `features = [...]` to `[features.<name>]`, and from
`path = "../x"` to that manifest's `[package]`.

### A dependency's features

A dependency's features are read from the text of its own manifest, the rules layer's ground: the
directory its `path` names, or for `workspace = true` the one the nearest enclosing workspace's
`[workspace.dependencies]` entry names. Its features are the names mcpp registers: the keys of
`[features]` (`default` among them) and `[features.<name>]`, and the names of `[feature-deps.<name>]`
and, under a target selector, of `feature-deps`, `feature-requires-abi` and `feature-xlings`. Its
`default` names the ones on without being named, an array or `implies` what each turns on. A
dependency whose manifest declares none accepts any name, as mcpp does. Between two elements a
suggestion brings its commas; an array left open is read on its own line only.

An index package's features are not answered: mcpp states none
([mcpp#992](https://github.com/mcpp-community/mcpp/issues/992) asks for them), and an index's
descriptors are mcpp's to read. `capabilities/explain` reports this as `manifest.packageFeatures`.
Hover on an element of the edge's `features = [...]` names the dependency, the default and what the
feature implies.

| Code | Default severity | Setting | What it reports |
|---|---|---|---|
| `mcpp.manifest.syntax` | error | `mcpp.toml.diagnostics.syntax` | a line that is neither a table header nor `key = value`; an invalid key; a key without a value |
| `mcpp.manifest.unknownSection` | warning | `mcpp.toml.diagnostics.unknownSection` | a table no section of the snapshot names, nor a prefix of it |
| `mcpp.manifest.unknownKey` | warning | `mcpp.toml.diagnostics.unknownKey` | a key a table with a closed vocabulary does not have |
| `mcpp.manifest.unknownFeature` | warning | `mcpp.toml.diagnostics.unknownKey` | a name in a `path` or workspace dependency's `features = [...]` that its manifest's `[features]` does not declare (1.6.0) |
| `mcpp.manifest.planeSeparation` | warning | `mcpp.toml.diagnostics.planeSeparation` | a tool payload (`xim:`, `xpm:`, `xlings:`, `xvm:`) as a dependency; an mcpp package with a version under `[xlings]` |
| `mcpp.manifest.legacyKey` | info | `mcpp.toml.diagnostics.legacyKeys` | a deprecated table or key, with its replacement |
| `mcpp.manifest.mcppFloor` | error | -- | `[package] mcpp` that is not a release floor (`">=2026.9.28.3"`) |
| `mcpp.manifest.arrayTable` | error | -- | an array of tables mcpp does not read (it reads `[[build.flags]]`, `[[runtime.requirements]]`, the snapshot's own) |

The scanner errs towards silence: the lines of an unclosed array, inline table or multi-line string
are not checked. `mcpp.toml.completion`, `mcpp.toml.hover`, `mcpp.toml.navigation` and
`mcpp.toml.diagnostics.enabled` turn each feature off.

## build.mcpp

| Code | What it reports (SPEC-007) |
|---|---|
| `mcpp.buildscript.unknownSymbol` | a `mcpp::<name>` that is no directive, action role or typed-API name of the snapshot, nor a namespace the file imports |
| `mcpp.buildscript.roleLiteral` | `role = "<role>"` where `mcpp::roles::<role>` exists (R3.6) |
| `mcpp.buildscript.actionNeedsRole` | an action without a role, or with a role the engine does not know (R3.2, R3.6) |
| `mcpp.buildscript.prepareNeedsOutputDir` | a prepare action without `output_dir` (R3.3) |
| `mcpp.buildscript.rpathInLinkFlag` | a run path in `link_flag` (R4.4) |
| `mcpp.buildscript.rawPathFlag` | `-I`/`-L` in a raw flag (R2.1) |
| `mcpp.buildscript.shellSyntaxInAction` | `VAR=value` or `cd x &&` in an action's command (R3.8) |

The analysis is line-oriented: comments and string literals (raw strings too) are masked, and an
action block runs from `mcpp::action` to `.submit(` within 40 lines. Completion offers the
directives' typed names and the API's snippets after `mcpp::`, and the modules a build program may
import (`std`, `std.compat`, `mcpp`, `mcpp.core`, `mcpp.plugins.*`, ...) after `import`; hover
describes a directive and a known module. The layer recognises module names and never reports one
as missing.

While C++ Modules (mcppls) serves the file -- the editor says so with `mcpplsServes` -- the backend
leaves it what a compiler answers better: the `unknownSymbol` rule, the module names after `import`
and their hover. `mcpp.buildScript.intelligence`, `.diagnostics` (with `.diagnostics.severity`),
`.snippets` and `.imports.knownModules` turn each part off.

## The snapshots and the tests

The rules read two snapshots of mcpp: `spec/toml-schema.json` (every table and key, each with the
release that added it) and `spec/buildscript-api.json` (the directives, roles and protocol), each
naming the mcpp release and commit it was taken from.

The layer is a port of mcpp-vscode 0.9's TypeScript implementation. `tests/cts/rules/golden` holds that
implementation's answers over `tests/cts/rules/corpus` -- the scanner's structure and the cursor's context
at every position of every line, completion, hover, the jumps, the version contexts and the
diagnostics under two sets of severities -- and over the documents of its parser tests
(`golden/snippets.json`); the conformance case `rules` (tests/cts/rules.cpp) compares the backend's answers with them.
The answers inside inline tables that 1.6.0 added are where 0.9 answered nothing; they are kept apart,
in `golden/inline-tables.json` (`MCPP_IDE_WRITE_GOLDEN=1 mcpp test cts` writes it again for review),
and every other position still equals 0.9's.
