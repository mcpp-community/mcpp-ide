---
name: mcpp-ide-contributing
description: Use when contributing to mcpp-ide -- reporting a bug, proposing a feature, opening an issue or a pull request, or making any change as a person or an AI agent. Covers triage (core or plugin, fix or workaround), issue and pull-request conventions, commits, review and merge.
---

# Contributing to mcpp-ide

Read [AGENTS.md](../../../AGENTS.md) first: who owns what, and the rules every change follows. The
flow is **issue → branch → pull request → CI → review → squash merge**. Nothing is pushed to `main`
directly, whatever its size, and that includes a version bump or a change to documentation.

## 1. Triage before anything else

Answer the three questions of AGENTS.md §3 and write the answers into the issue:

| Question | How to decide | Outcome |
|---|---|---|
| Whose problem? | reproduce without the editor: `mcpp-ide self-check --json`, `mcpp-ide serve`, the mock mcpp | mcpp, mcppls or an editor's upstream: report it there and link it here |
| Core or plugin? | would another editor need the same change? | yes → backend (`src/`, `spec/`, `protocol/`); no → `mide-plugins/<editor>` |
| Fix or workaround? | does the cause lie in this repository? | no → no workaround by default; an exception states its cause, the upstream issue, why it cannot wait and when it is removed |

## 2. Issues

Open one with a template (`.github/ISSUE_TEMPLATE/`): **Bug report**, **Feature request** or
**Task**. A blank issue is not offered, and a question goes to the
[forum](https://forum.d2learn.org/category/20).

- Title: `<type>(<component>): <sentence>`, for example `fix(clion): the test tree loses a case's
  location after a rebuild`. The type is one of `feat`, `fix`, `refactor`, `perf`, `test`, `docs`,
  `ci`, `chore`. The component is `backend`, `vscode`, `clion`, `nvim`, `docs` or `ci`.
- A bug carries the output of `mcpp-ide self-check --json` (backend, mcpp, mcppls, xlings, trust),
  the editor and its version, the steps, the expected and actual behaviour, and the triage answers.
  The IDE report (**mcpp: Export IDE Report**) is attached when the backend is involved.
- A feature states the motivation, which party it belongs to, the protocol or `spec/` change it
  needs, and what each editor shows. A change larger than one pull request gets a design record in
  `.agents/docs/` (`YYYY-MM-DD-<subject>.md`, opening with its status) before any code.

## 3. Branches and commits

- Branch from the latest `main`: `<type>/<subject>`, for example `fix/clion-case-location`. A
  release is made on `release/X.Y.Z` (see [mcpp-ide-development](../mcpp-ide-development/SKILL.md)).
- A commit is English and declarative, `<type>(<component>): <sentence>` or `X.Y.Z: <sentence>` for
  a release, with no emoji. Its body says why, citing the issue, the design record or mcpp's spec
  clause where there is one.

## 4. Pull requests

- One pull request does one thing and links the issue it closes (`Closes #N`). The title follows
  the commit form, since it becomes the subject of the squash merge.
- The body follows `.github/pull_request_template.md`: summary, triage, the criteria (the test that
  fails before and passes after, and where it ran), compatibility and the checklist.
- A change of behaviour updates, in the same pull request: `protocol/protocol.schema.json`, the docs
  it touches (README.md and README.zh-CN.md together), `spec/i18n` in both languages, and
  CHANGELOG.md under its series.
- The required check is `ci`. It runs the components the change reaches (`.github/components.tsv`),
  so a backend change runs every plugin. A red job is fixed, never retried until green.
- The merge is a squash with an explicit subject and body; GitHub's composed message is not used.

## 5. AI agents

An agent contributes under the same rules, plus these:

- **Read before writing:** AGENTS.md, the docs it links for the area, and the neighbouring code. Do
  the triage before editing, and say in the pull request which party the problem belongs to.
- **Ask before outward actions:** creating an issue, a pull request, a comment, a tag or a release
  needs the person's confirmation. Never push to `main`, force-push a shared branch, merge, tag or
  publish unless explicitly asked.
- **Report what ran:** each criterion says whether it ran locally, on which system, or was left to
  CI. A test that did not run is not described as passing.
- **Stay in scope:** no drive-by refactors, no new dependency and no workaround for another party's
  problem without the person's decision.
- **No attribution trailers:** commits and pull requests carry no `Co-Authored-By` lines or
  generated-by footers. The person who submits a change is responsible for it.
