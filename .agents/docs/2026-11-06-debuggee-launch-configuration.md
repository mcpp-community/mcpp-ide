# Debug launches from the selected build configuration

Status, 2026-11-06: implemented in the current core and IDE worktrees. Local
core/backend controls and compilation against both stable CLion SDKs pass.
Final public-group CI, packaged IDE runtime and release qualification remain
separate requirements.

## Responsibility and selection

The core describes the program that `run` or `test --print-launch` actually
selected. The additive `configuration` object contains its triple, profile,
resolved platform and available named runners. The runner names remain
available when a project uses no active platform. This avoids querying a
default configuration separately or taking the first leg of a target-set
environment report, neither of which identifies the program selected to run.

The IDE first asks for that ordinary launch. If it declares `debuggee` and the
request did not explicitly choose a runner, the IDE asks for that named runner
with the selected triple and profile. A different triple, profile or platform
in the second answer is refused. An explicit runner, including an explicit
empty/default runner, takes precedence. A device action named `debug` is not
interpreted as a host-debugger launch. Older cores that omit the additive
configuration retain their ordinary behavior.

The core assigns no OpenKal meaning to `debuggee`. Platform 0.24 declares
`okrun --debugger` through the existing named-runner mechanism. `mcpp test`
accepts `--runner` through the same resolver as `run`, including launch
descriptions and archive execution. A missing name or a simultaneous
`--no-runner` is refused. The core feature version is 2026.11.6.1.

## Editor behavior

Custom profile names are passed with `--profile`; the CLion run-configuration
profile selector accepts typed values. CLion obtains the backend's debug
configuration for its selected native debugger. The adapter enables LLDB's
GDB JIT reader in that session before loading the target. It does not change
a user's global debugger initialization file. Other runners keep their own
semantics and do not implicitly promise source symbols.

The backend describes both program and individual-test debug launches. The
existing CLion debug executor remains limited to program run configurations;
no CLion test-debug UI is claimed by the backend protocol tests.

## Validation and current boundaries

The actual protocol test drives the mock executable through the backend's
wire. It covers older-core fallback, ordinary board actions, target-set
selection, a custom profile, program and test paths, explicit runner
precedence, missing configuration and changed target/profile/platform refusal.
It also checks space-containing and empty arguments.

A separate native integration starts the actual backend with the newly built
core and a real project. Eleven checks cover ordinary and debug launches for
both a program and a test: actual profile and runner declarations, unchanged
configuration, no execution while describing a launch, execution of the
returned argv, argument preservation and explicit default-runner precedence.
It uses no mock and does not stand in for CLion GUI or JIT qualification.

The fresh core passes E2E 01, 333, 993 and 936. E2E 993 includes the actual
named runner, archive execution, missing-name and contradictory-option
controls. E2E 936 initially inherited `NO_COLOR=1`; removing that external
setting passes the unchanged color-precedence checks. The core build uses
the OS 0.3 candidate source path and is not final public dependency evidence.

The changed Kotlin adapters compile against CLion 2026.1.5.2 and 2026.2.4.1.
Compilation does not establish packaged runtime behavior. Image source
debugging still requires retained debug relocations and discoverable JIT
registration symbols in the packaged runner. The source-runner GDB/LLDB
controls and the precompiled-runner packaging qualification are separate.
The independently reproduced CLion 2026.2 native frame-variable cancellation
error remains separate from these changes and is not suppressed.
