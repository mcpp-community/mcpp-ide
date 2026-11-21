# Versions and compatibility

## Series

A version is `X.Y.Z`. The series `X.Y` is shared by the backend and every plugin. A plugin works
with any backend of its series and refuses one from another series, naming both versions and the
install that matches.

The VS Code and CLion packages carry the backend, so a change to the backend changes every package.
It therefore begins a new series: every component moves to `X.Y.0`, tagged `vX.Y.0`, and the
backend is always its series' `X.Y.0`. `Z` is an editor plugin's own patch, tagged
`vscode-vX.Y.Z`, `clion-vX.Y.Z`, `nvim-vX.Y.Z` or `zed-vX.Y.Z`. Such a tag publishes that plugin alone over its
series' backend; VS Code 1.3.2 and CLion 1.3.1, for example, both carry backend 1.3.0. A plugin
patch is refused when any backend path has changed since its series' tag
([release-plan.sh](../.github/scripts/release-plan.sh)).

## The project's mcpp

Each project is described by the mcpp that builds it: the one its `.xlings.json` pins, or
`mcpp.path`. A feature is enabled by what that mcpp states (`features`). An earlier release is
described by a table frozen at that release, never by comparing versions. A view that needs a
feature the project's mcpp does not state is left out, and the self-check (`capabilities/explain`)
names the mcpp that states it. If `mcpp` is absent, VS Code and CLion show one notification linking
to the [mcpp repository](https://github.com/mcpp-community/mcpp); they look for mcpp only when an
action needs it.

## Tests

The editors show tests as `mcpp test` states them ([architecture.md](architecture.md), "A test
run, end to end"). A program made of several files (`[test] programs = "directory"`) lists its
files as children, each file being the part of the program that a run gives a process. A case of
the mtest protocol carries its kind, its tags and the bounds on its metrics. Test Insights reads
the runs mcpp keeps in `target/mtest`: the last run, its failures and when they began, each metric
against its bound, the slowest and the flaky cases, and the schedule with its critical path.

| The project's mcpp | Adds |
|---|---|
| any | programs and their results |
| 2026.11.8.1 (`test-protocol`) | cases, their places and results, both sides of a failure |
| 2026.11.9.1 | the files of a multi-file program; Test Insights over the kept runs |
| `test-parts`, `test-case-stream`, `test-metrics` ([mcpp#917](https://github.com/mcpp-community/mcpp/issues/917)) | the parts as mcpp states them, results as each case ends, metrics and their trends |
| `test-launch-records` (2026.11.10.1) | cases run through a framework's test session |
