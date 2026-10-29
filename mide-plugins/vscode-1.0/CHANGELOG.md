# Changelog

## 1.0.0

- The extension is the VS Code adapter of mcpp-ide: its decisions are the mcpp-ide backend's
  (`mcpp-ide serve`, shipped inside the extension), which reads mcpp's control contract. The same
  features exist in CLion through mcpp-clion.
- Builds and tests run as tasks in a pseudoterminal that shows mcpp's own narration; closing the
  terminal cancels the build. Problems come from mcpp's located diagnostics, not from a pattern.
- The Testing view lists every test of every project (`mcpp test --list`) and runs one or all.
- Debugging through a launch configuration of type `mcpp`, for CodeLLDB, the C/C++ extension,
  lldb-dap or Native Debug; an ok image is debugged through `okrun`.
- Targets and platforms: the targets mcpp knows, their toolchains and the platform that configures
  each; `mcpp.build.target` chooses the target build, run, test, debug and pack use.
- mcpp.toml's problems come from mcpp; the extension's own TOML checks are removed and their
  settings deprecated.
- mcppls is installed with the extension as an extension pack rather than required; the project
  view reports whether the installed mcppls reads mcpp's result record (0.0.18 and later).
