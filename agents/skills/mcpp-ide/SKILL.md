---
name: mcpp-ide
description: Drive an mcpp C++ project through the mcpp-ide backend (`mcpp-ide serve`, JSON-RPC on stdio) -- list tests without building, run builds and tests as serialized jobs with normalized results, inspect targets and the platform configuring each, plan cache cleanups with their danger level, and read package/module graphs. Use when a task needs more than `mcpp build`/`mcpp test` output, or the same facts an mcpp IDE panel shows.
---

# mcpp-ide

The backend reads only mcpp's machine contract; it parses no mcpp.toml and resolves no
dependency. Its answers are the editors' answers.

## Start

1. `mcpp-ide self-check --json` -- is mcpp found, does it have the control contract (2026.10.26.1+),
   which mcppls answers C++ files.
2. `mcpp-ide serve`, then `initialize` with `{"editor": "cli", "workspaceFolders": [<dir>]}`.
   Frames: `Content-Length: <n>\r\n\r\n<json>`, both ways.

## Methods worth knowing

| Method | Answers |
|---|---|
| `test/list {root}` | every test, its workspace member and the file holding its `main`; no build |
| `job/start {kind: build\|test, root, target?, profile?, filter?}` | a job id; then `job/output`, `job/plan`, `job/diagnostic`, `test/result`, `test/summary`, `job/finished` |
| `job/cancel {jobId}` | mcpp ends the steps it started; the result record says `cancelled` |
| `targets/list {root}` | each target, its toolchains, `installed`, and `platform` (name, version, source, fields) |
| `project/summary {root}` | executables, tests, libraries, last build, the platforms the last plan named |
| `run/launch {root, target?}` | builds; argv, env and cwd that start the program |
| `cache/plan {action}` | argv, danger `level` (0-3), `preview`, whether acknowledgement is required |
| `graph/packages {root}`, `graph/modules {root}` | nodes and edges |
| `manifest/check {root}` | mcpp.toml's problems, located |

## Reading results

- `job/finished.status`: `ok`, `failed`, `cancelled`, `denied` (an effect was denied: exit code 5),
  `superseded` (a newer job of the same kind replaced it).
- `test/result.status`: `passed`, `failed`, `timedOut`, `skipped`, `errored`; `output` is the
  test's compile and run output.
- Diagnostics are 1-based `{line, column}`, with `path` absolute.
- A cleanup at level > 0 is applied only with `cache/apply {action, confirmation: {confirmed,
  previewShown, acknowledged}}` -- ask the person before you set those.

## Do not

- Do not edit mcpp.toml to add a dependency: `library/add` runs `mcpp add`.
- Do not run cleanups of level 2 or 3 without the person's explicit consent: they remove entries
  other projects on the machine use.
