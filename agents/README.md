# mcpp-ide for agents

An agent drives an mcpp project the way the editors do: through the mcpp-ide backend, whose
protocol is JSON-RPC on the standard streams of `mcpp-ide serve`
([protocol/protocol.schema.json](../protocol/protocol.schema.json)). The backend answers the same
questions the panels ask -- which tests a project has, what a cleanup would remove, which platform
configures a target -- from mcpp's machine contract, so an agent and a person in the editor see the
same facts.

An agent that only builds and tests needs none of this: `mcpp build --message-format json` and
`mcpp test --message-format json` are mcpp's own contract (mcpp docs/50, docs/52). The backend is
for what mcpp leaves to its clients: one job at a time per project, cleanup plans with their
danger levels, views of the cache, the libraries and the graphs, and the language service's state.

## Getting the backend

- `xlings install mcpp-ide`, or
- from a clone: `mcpp build`, then `target/<triple>/<fingerprint>/bin/mcpp-ide`.

`mcpp-ide self-check` (or `self-check --json`) reports what the backend finds: the backend, mcpp
and its contract, mcppls, xlings, trust.

## A session

```mermaid
%%{init: {"theme": "base", "themeVariables": {"fontFamily": "ui-sans-serif, system-ui, -apple-system, Segoe UI, sans-serif", "fontSize": "14px", "actorBkg": "#ddf4ff", "actorBorder": "#54aeff", "actorTextColor": "#0550ae", "actorLineColor": "#b6e3ff", "signalColor": "#54aeff", "signalTextColor": "#57606a", "sequenceNumberColor": "#ffffff", "noteBkgColor": "#fbefff", "noteBorderColor": "#c297ff", "noteTextColor": "#6639ba", "labelBoxBkgColor": "#ddf4ff", "labelBoxBorderColor": "#54aeff", "labelTextColor": "#0550ae", "loopTextColor": "#57606a"}}}%%
sequenceDiagram
    participant A as Agent
    participant B as mcpp-ide serve
    A->>B: initialize {editor: "cli", workspaceFolders: [dir]}
    B-->>A: projects, mcpp, languageService, checks
    A->>B: test/list {root}
    B-->>A: programs, their files and parts
    A->>B: job/start {kind: "test", root, color: false}
    B-->>A: job/started, job/output, job/plan
    B-->>A: test/result ..., test/summary
    B-->>A: job/finished
    A->>B: shutdown, then the notification exit
```

Each message is framed as in LSP: `Content-Length: <n>\r\n\r\n<json>`.

## The skill

[skills/mcpp-ide/SKILL.md](skills/mcpp-ide/SKILL.md) is a skill for agents that load skills
(Claude Code and others): when to use the backend, the methods worth knowing, and how to read the
answers.
