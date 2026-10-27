# mcpp-clion

mcpp's project features in CLion 2025.2 and later, over the mcpp-ide backend:

- the **mcpp** run configuration type: run (the program in a PTY console), test (the IDE's test
  tree) or build, for a target and a profile; a ▶ beside `main` runs the project, or the test whose
  `main` it is;
- **debugging** under CLion's own GDB or LLDB (the default toolchain's); an ok image runs under
  `okrun`, whose images reach GDB through the JIT interface;
- **console links** to `file:line:column`;
- the **mcpp tool window**: project, libraries, cache, targets and platforms, and the graphs, pack
  and settings panels (Settings | Tools | mcpp);
- the **Tools | mcpp** menu, generated from `spec/commands.json`.

Language features of C++ files come from the mcppls CLion plugin, which this plugin depends on; it
registers no language service.

## Build

```bash
xlings install gradle            # Gradle and its JDK
mcpp build                       # in the repository root: the backend and mcpp-ide-dev
gradle buildPlugin               # build/distributions/mcpp-clion-<version>.zip
gradle verifyPlugin              # CLion 2025.2 and the version in gradle.properties
```

What the archive carries of the backend: `MCPP_IDE_OKB` (the ok-image bundle, as
`backend/mcpp-ide.okb`) with `MCPP_IDE_OKRUN` (okrun laid out `<os>-<arch>/okrun[.exe]`), or, until
the bundle is built, `MCPP_IDE_BACKENDS` (a program per system laid out `<platform>/mcpp-ide[.exe]`:
`linux-x64`, `linux-arm64`, `darwin-arm64`, `win32-x64`). Without them the plugin finds the backend
through `mcpp.ide.backendPath`, xlings or PATH.

## Tests

`gradle test` runs the wire's tests, and a session over the mock mcpp when `MCPP_IDE_BACKEND` and
`MCPP_IDE_MOCK_MCPP` name the programs `mcpp build` produced.
