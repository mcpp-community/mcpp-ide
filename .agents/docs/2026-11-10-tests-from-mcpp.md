# Tests as mcpp states them: programs, parts, cases, live results, metrics and history

Status, 2026-11-10: the design of mcpp-ide 1.3.0's tests. The client side of mcpp#917 is part of it.

## Scope and principle

The editors show tests as `mcpp test` states them. mcpp builds and runs the programs. mtest's protocol
is how a program's cases report to mcpp. mtest's store (spec P12) is the form in which mcpp keeps a run
(`target/mtest`). The backend reads three inputs:

- the records of `mcpp test --message-format json`;
- the listings of `--list` and `--cases`;
- the store.

It does not drive mtest's engine. A program that only mtest's own runner runs (a `[discover]` script of
`mtest.toml`) is outside `mcpp test`, and the editors do not show it.

Each editor presents the same data in its own idiom:

- VS Code uses the Testing view, test messages, and editor decorations.
- CLion uses the SMRunner test tree and its comparison view.
- Neovim uses buffers, signs, virtual text, and the quickfix list.

An editor shows only what the project's mcpp provides. A missing input removes the view that needs it.
It does not remove the views that do not need it.

## What mcpp provides, by version

| mcpp | Provides | The editors show |
|---|---|---|
| before 2026.11.8.1 | one record per program | programs and their results |
| 2026.11.8.1 (`test-protocol`) | `listed` and `cases` of a protocol program | cases, their results at the program's end, their failures' places, `kind`, `tags`, `expect` and `expected`/`actual` |
| 2026.11.9.1 | a protocol program of several files runs as one process per file (`<program>/<file>`); the run is kept in `target/mtest` | a program of several files with its files as children; results of a part attach to its program; history, trends of durations, and the schedule from the store |
| `test-parts` (mcpp#917) | `files`, `parts`, `main: null`, `part_of` | the same tree, from mcpp's statement instead of the backend's derivation |
| `test-case-stream` (mcpp#917) | `case` records as cases begin and end | live results per case |
| `test-metrics` (mcpp#917) | `metrics`, `bounds`, `usage`; the store's P12 fields | metrics beside their bounds, their trends, and a case-level schedule |

A view that needs an input names the missing feature in `capabilities/explain` (`tests.parts`,
`tests.live`, `tests.metrics`).

## The model

A **program** is what `mcpp test --list` names: `unit` for the directory `tests/unit/` under
`[test] programs = "directory"`, and `00-a/0` for one file. Its fields are:

- `files`: its own sources;
- `main`: the file that defines `main`, if one does;
- `layout`: `file` or `directory`;
- `parts`: the processes a run divides it into, each `{name, file}`.

With `test-parts`, these fields are mcpp's. Without it, the backend derives them from what mcpp
2026.11.9.1 states (docs/08):

- A program whose `main` file lies in a directory whose path below the test root is the program's name
  is of layout `directory`.
- Its files are the C++ sources directly in that directory.
- Its parts are `<program>/<stem>`, one per file.
- `main` is the file whose source defines `main`, found by the scanner; otherwise it is absent.

A **case** belongs to a program and a file. Its fields are `id`, `file`, `line`, `kind`, `tags`,
`expect` and, with `test-metrics`, `bounds`. Static cases are read from each file with mtest's manifest.
A listing (`--cases`) replaces them case by case.

A result names the program it belongs to:

- `part_of` when mcpp states it;
- else the longest listed program whose name is the record's name or a prefix of it followed by `/`;
- else the record's name.

## The backend's protocol (protocol 2, additions only)

- `test/list`: each test gains `files`, `layout`, `parts`, and `main` as described above.
- `source/entries`: a file of a directory program answers `test` (the program) and `part` (its own
  part).
- `test/cases`: reads every file of the program. Each case gains `file`, `kind`, `tags`, `expect` and
  `bounds`.
- `test/result` gains:
  - `program` and `part`;
  - `cases`, each with `id`, `status`, `durationMs`, `reason`, `failures` (`path`, `line`, `message`,
    `expected`, `actual`), `metrics`, `bounds`, `kind` and `tags`.

  A case's status is mapped as follows:

  | mcpp | protocol |
  |---|---|
  | `pass` | `passed` |
  | `fail` | `failed` |
  | `skip` | `skipped` |
  | `xfail` | `passed` with `expectedFailure` |
  | `xpass` | `failed` with `unexpectedPass` |
  | `invalid` | `errored` |

  A whole-program run therefore marks every case.
- `test/caseResult`: gains the fields of a case above. It is sent for every record of the job, not
  only the last one; a run of a directory program writes one record per part.
- `test/caseEvent` (new): `{jobId, root, program, part, case, event: "begin" | "end", tMs, ...}` with
  the fields of a case on `end`. It is sent as mcpp writes a `case` record (`test-case-stream`).
- `test/history {root, program?, case?, runs?}` (new): the store's last runs (20 by default) and each
  selected case's results across them.
- `test/insights {root, runs?}` (new):
  - the last run: its counts and its counts by kind;
  - its failures, each with the run since which it has failed;
  - the slowest cases, with their median over the window;
  - the cases whose status changed within the window (flaky);
  - each metric's series, its bounds, its last value and its margin;
  - the schedule: one lane per worker, each process with its start, duration and build, and the
    critical path (the chain of processes on one worker that ends last, with the build before it).
- `capabilities/explain` gains `tests.parts`, `tests.live` and `tests.metrics`.

The store is read from `[test] store` when the manifest sets it, else from `target/mtest`. The backend
reads it as P12 specifies it. It reads `index.ndjson`, then the last N runs' files, and keeps them by
run id. A run already read is not read again. A store that is absent is not an error: the history and
insights are empty and say so.

## Each editor

**VS Code.**

- The Testing view shows each project, then its programs, then the files of a directory program, then
  the cases.
- Each case carries the test tags `kind:<kind>` and its own tags, so `@kind:bench` filters the view.
- Results arrive per case as they happen (`test/caseEvent`), else per part.
- A failure with `expected` and `actual` is a `TestMessage.diff`, and VS Code's diff view compares them.
- After a run, a case's line ends with a dim decoration: its duration, and its metrics against their
  bounds (`ready 4.2 s ≤ 15 s`).
- "mcpp: Test Insights" is a webview with these sections:
  - the run's summary;
  - its failures, each opening its place;
  - the slowest cases;
  - the flaky cases;
  - each metric's trend against its bounds;
  - the schedule, with its critical path.

  It refreshes when a run ends.

**CLion.**

- The SMRunner tree shows a program as a suite, the files of a directory program as nested suites, and
  the cases as tests, each located at its line.
- Live case events start and finish tests as they come.
- A failure with `expected` and `actual` is a comparison failure, whose "Click to see difference" opens
  the IDE's diff.
- Metrics appear in a case's output beside their bounds.
- The mcpp tool window gains a Tests tab: the same insights in JCEF (`webui/tests.html`), as the graph
  page is drawn.

**Neovim.**

- `mcpp://tests` gains sections per program, part and case as results arrive, live with
  `test-case-stream`.
- A failure's `expected` and `actual` appear as two lines under it, and in its quickfix text.
- A case's line carries its sign (✓, ✗) and, as virtual text, its duration and its metrics against
  their bounds.
- `:Mcpp testInsights` opens the insights as text in a floating window. A metric's trend is drawn with
  block characters (▁▂▃▅▇), and the schedule as bars.

## Performance

- The suite types of a project's tests are read once and kept with a signature of the files: their
  count, their largest modification time, and their total size, from a walk that only stats.
  `source/entries` and `test/cases` no longer read every file of `tests/` on each call.
- Live results are sent as they come. VS Code and CLion apply them as their test APIs do.
- Neovim redraws the results buffer at most every 100 ms.
- The store is read incrementally: only runs not read before.
- The insights window is bounded: 20 runs by default, 50 at most.

## Tests

- Backend:
  - the derivation of parts from a 2026.11.9.1 listing;
  - the attribution of part records to their program;
  - the mapping of the protocol's statuses;
  - `expected`/`actual`;
  - `case` records into `test/caseEvent`;
  - the store reader, and the insights (the window, flaky, the critical path, the margins);
  - the schema of the new methods and notifications.
- The mock mcpp gains the fixtures of a directory program, with and without the #917 shapes. A
  directory's `.mock-mcpp-tests` selects them, as `.mock-mcpp-features` selects the features.
- VS Code: the tree of a directory program; part results attributed to it; the diff message; the
  decorations' text.
- Neovim: the smoke test covers live results, the virtual text and `:Mcpp testInsights`.
- CLion: the comparison failure and the nested suites, in the plugin's unit tests.
- The insights page is rendered by a Chromium test, as the graph page is.
