# mcpp-clion

mcpp's project features in CLion 2026.1 and later, over the mcpp-ide backend:

- the **mcpp** run configuration type: run (the program in a PTY console), test (the IDE's test
  tree) or build, for a target and a profile; a ▶ beside `main` runs the project, or the test whose
  `main` it is;
- **debugging** under CLion's own GDB or LLDB (the default toolchain's), using the
  program and arguments returned by the backend's debug launch;
- **console links** to `file:line:column`;
- the **mcpp tool window** in CLion's own components: the Project tab (each project's mcpp,
  targets, tests, libraries, last build, artifacts, platform, and what C++ Modules says), the
  Libraries tab (the indexes' packages, a package's page, adding it to the project), the Cache tab
  (what mcpp keeps, and the cleanups with their confirmations), and a graph of the packages or the
  modules as a page in JCEF; **Settings | Tools | mcpp** is a form generated from the backend's
  settings registry, with a search;
- **Select the Build Target** (target sets among the rows) and **Pack** as popups, packing as a job;
- the **Tools | mcpp** menu, generated from `spec/commands.json`.

Language features of C++ files come from the mcppls CLion plugin, which this plugin depends on.
Those of `mcpp.toml` and `build.mcpp` -- completion, documentation, the jumps and the diagnostics --
come from the backend over the session's connection (mcpp-ide's `docs/rules.md`).

When a user starts an action that needs `mcpp` and the tool is absent, CLion shows one notification
with the [mcpp repository](https://github.com/mcpp-community/mcpp) and an action to open it. Opening
a project alone does not show that notification.

In **Settings | Tools | mcpp**, Apply validates every numeric value against the registry's
range and rejects non-finite values. An invalid value displays an error and leaves all stored
settings unchanged. Entering a setting's default removes its stored override.

The graph view reports a failed build-database query as an error. Correct the reported problem
and reopen the graph to retry. An unavailable JCEF browser displays an explanation in the tool window.

## Debug launches

The run configuration's Profile field accepts `dev`, `release` and custom
profile names. An empty field uses the project setting.

With mcpp 2026.11.6.1 or later, the backend uses the configuration returned by
the actual program launch. If that configuration declares a named `debuggee`
runner, the backend selects it with the same target and profile. Other runner
names, including device actions named `debug`, are not selected automatically.
Older mcpp versions retain their ordinary launch behavior.

OpenKal platform 0.24.0 declares `debuggee` as `okrun --debugger`. Image source
debugging requires retained debug relocation information and the runner's JIT
registration symbols. The adapter enables LLDB's GDB JIT reader for the debug
session; no change to a global LLDB initialization file is needed. External
debug sidecars are not supported by this path.

The backend protocol also describes individual test debug launches through
`mcpp test --runner`. The CLion adapter currently offers native debugging for
program run configurations; this does not claim a CLion test-debug UI.

## Build

```bash
xlings install gradle            # Gradle and its JDK
mcpp build                       # in the repository root: the backend and mcpp-ide-dev
(cd ../vscode && npm ci)         # the extension's dependencies: the build bundles the graph page with them
gradle buildPlugin               # build/distributions/mcpp-clion-<version>.zip
gradle verifyPlugin              # CLion 2026.1 and the version in gradle.properties
```

What the archive carries of the backend: `MCPP_IDE_OKB` (the ok-image bundle, as
`backend/mcpp-ide.okb`) with `MCPP_IDE_OKRUN` (okrun laid out `<os>-<arch>/okrun[.exe]`); see
docs/architecture.md, Distribution. Without them the plugin finds the backend through
`mcpp.ide.backendPath`, xlings or PATH.

## Tests

`gradle test` runs the wire's tests, a session over the mock mcpp when `MCPP_IDE_BACKEND` and
`MCPP_IDE_MOCK_MCPP` name the programs `mcpp build` produced, and the views' models and the
settings form headless. `npm run test:webui` in `../vscode` runs the graph page in Chromium over the
backend.

The stable IDE tests also use the actual settings dialog: keyboard input, visible validation
errors, refusal of partial saves, the persisted XML, reopening the form and restoring defaults.
Graph tests inspect the real JCEF document, click a rendered module to open its source,
check visible query errors and recovery, and check the explanation when JCEF is disabled.

The packaged ZIP is also run in the real stable CLion 2026.1.5.2 and 2026.2.4.1 IDEs by
`clion-stable.yml`. Both versions load the same ZIP and the pinned mcppls plugin. The test opens
an mcpp project, completes protocol 2 initialization, loads runtime translations and commands,
and verifies the visible mcpp tool window and cache inventory. It executes build, run and test
actions in a project whose path contains Unicode and spaces, launches the native debugger,
checks its visible view, source breakpoint, variable values before and after stepping, resume
and stop, checks the native test tree,
compiler-error output, build/test failure and recovery, and exact manifest completion insertion
during project analysis. It supplements Plugin Verifier's static compatibility checks.
CLion 2026.2.4.1 can emit a GDB variable-read cancellation after stopping a
debug session; the same exception occurs in a stock CMake project without the
plugins. The stable test records the exception class and method symbols, and
classifies this one SDK event only after its functional assertions complete.
Other IDE errors fail the test.

An individual packaged plugin can be checked without rebuilding the backend:

```bash
xvfb-run -a gradle --no-daemon -p integration-tests test \
  -PpluginZip=/absolute/path/mcpp-clion-1.4.0.zip \
  -PmcpplsZip=/absolute/path/mcppls-clion-0.21.0.zip \
  -PideVersion=2026.1.5.2
```

The CLion session writes backend selection and startup failures to `idea.log`, under
`io.github.mcppcommunity.mcppclion.McppSession`. A protocol mismatch stops initialization before
custom requests. For an explicitly configured older backend, update it or clear
`mcpp.ide.backendPath` to select the bundled backend. Error notifications can also appear in
the IDE's notification area.
