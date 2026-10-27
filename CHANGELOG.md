# Changelog

## 0.1.0

The first release of mcpp-ide: one backend for mcpp's editor integrations, the VS Code extension
(mcpp-vscode 1.0.0, the same extension ID as before) and the CLion plugin (mcpp-clion 0.1.0).

- Backend (`mcpp-ide serve`): sessions, watching, jobs, view models, cleanup plans, settings, the
  command table, runtime strings, self-check; handshakes with mcpp and mcppls; targets with their
  platforms.
- Protocol and contract schemas, held against mcpp 2026.10.26.2's real output.
- Shared panels: project, targets and platforms, libraries, dependency and module graphs, cache,
  pack, settings.
- VS Code: tasks, Testing view, debug configurations, Problems from mcpp, mcpp.toml editing help.
- CLion: run configurations with a PTY console, test tree, CLion's debugger, console links, the mcpp
  tool window; depends on the mcppls CLion plugin.
- One package per editor for every system: the backend as an ok-image bundle with okrun per system
  (or a program per system until the bundle is built); CI starts it from both packages on Linux
  x64/arm64, macOS and Windows.
- `mcpp pack`'s package format is passed as `--pkg-format` where mcpp offers it (`--format` before).
