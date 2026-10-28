# Changelog

## 0.1.0

The first release of mcpp-ide: one backend for mcpp's editor integrations, the VS Code extension
(mcpp-vscode 1.0.0, the same extension ID as before) and the CLion plugin (mcpp-clion 0.1.0).

- Backend (`mcpp-ide serve`): sessions, watching, jobs, view models, cleanup plans, settings, the
  command table, runtime strings, self-check; handshakes with mcpp and mcppls; targets with their
  platforms.
- Protocol and contract schemas, held against mcpp 2026.10.27.1's real output (target rows naming
  their platform, a plan's platform and host platform, a wrapped program's launch).
- Shared panels: project, targets and platforms, libraries, dependency and module graphs, cache,
  pack, settings.
- VS Code: tasks, Testing view, debug configurations, Problems from mcpp, mcpp.toml editing help.
- mcpp.toml and build.mcpp editing help over mcpp 2026.10.27.1's keys and directives: `[platform]`
  and its tables, `[host]`, a selector's platform overrides (`platform`, `carrier`, `layers`,
  `tools`, `link`, `runners`, `run_under`), `[pack]`'s `linkage`, `format`, `compress`, `sign` and
  `universal`; the protocol 17 directives, completed by the typed API's names (`mcpp::link_driver`).
- CLion: run configurations with a PTY console, test tree, CLion's debugger, console links, the mcpp
  tool window; depends on the mcppls CLion plugin.
- One package per editor for every system: the backend as one ok-image bundle (x86_64 and aarch64
  images on openkal-platform 0.17.3) with okrun for Linux x86_64/aarch64, macOS arm64 and Windows
  x64; CI starts it from both packages on each of those systems.
- `mcpp pack`'s package format is passed as `--pkg-format` where mcpp offers it (`--format` before).
