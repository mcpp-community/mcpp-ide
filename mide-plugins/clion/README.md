# mcpp-clion

mcpp's project features in CLion 2026.1 and later, over the mcpp-ide backend:

- the **mcpp** run configuration type: run (the program in a PTY console), test (the IDE's test
  tree) or build, for a target and a profile; a ▶ beside `main` runs the project, or the test whose
  `main` it is;
- **debugging** under CLion's own GDB or LLDB (the default toolchain's); an ok image runs under
  `okrun`, whose images reach GDB through the JIT interface;
- **console links** to `file:line:column`;
- the **mcpp tool window**: mcpp-vscode's views and pages -- the project tree, the libraries and a
  package's page, the cache, the dependency and module graphs -- in a workbench (JCEF) whose tabs
  are the views; the settings page is Settings | Tools | mcpp;
- **Select the Build Target** (target sets among the rows) and **Pack** as popups, packing as a job;
- the **Tools | mcpp** menu, generated from `spec/commands.json`.

Language features of C++ files come from the mcppls CLion plugin, which this plugin depends on; it
registers no language service.

## Build

```bash
xlings install gradle            # Gradle and its JDK
mcpp build                       # in the repository root: the backend and mcpp-ide-dev
(cd ../vscode && npm ci)         # the extension's dependencies: the build bundles the workbench with them
gradle buildPlugin               # build/distributions/mcpp-clion-<version>.zip
gradle verifyPlugin              # CLion 2026.1 and the version in gradle.properties
```

What the archive carries of the backend: `MCPP_IDE_OKB` (the ok-image bundle, as
`backend/mcpp-ide.okb`) with `MCPP_IDE_OKRUN` (okrun laid out `<os>-<arch>/okrun[.exe]`); see
docs/architecture.md, Distribution. Without them the plugin finds the backend through
`mcpp.ide.backendPath`, xlings or PATH.

## Tests

`gradle test` runs the wire's tests, and a session over the mock mcpp when `MCPP_IDE_BACKEND` and
`MCPP_IDE_MOCK_MCPP` name the programs `mcpp build` produced. `npm run test:webui` in
`../vscode` runs the workbench in Chromium over the backend, answering its bridge as Workbench.kt
does.
