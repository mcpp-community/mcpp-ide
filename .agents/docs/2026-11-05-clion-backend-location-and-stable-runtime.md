# CLion backend location and stable runtime validation

Status: implemented on the review branch; stable CI validation pending.

## Observed failure

The published mcpp-clion 0.2.0 package contains `backend/mcpp-ide.okb` and its runners. Its
`pluginDirectory()` nevertheless derives the installation directory from the class loader's
`CodeSource.location`. A full local CLion 2026.1.5.2 run, build CL-261.27258.141, verified that
the actual `McppSession` class has a null location. The original package consequently fails
to find the bundled backend in an isolated environment.

A separate user log records downloading 0.2.0, dynamically loading the plugin, and starting a
backend with source `xlings`. It records neither the external backend's version nor the
unknown-method response. The log establishes fallback selection, not the selected backend's
protocol. No user log excerpts, account names, project names or local paths are included here.

An independent wire experiment with the real released 0.1.0 backend reproduces error -32601
for `mcpp-ide/i18n/bundle`; that backend reports protocol 1. The 0.2.0 bundled backend reports
protocol 2 and answers the request. This is a sufficient reproduction of the notification,
not proof of the user's external backend version. The shorter reported `mcpp-ide/bundle`
spelling has not been observed on the wire; the current client sends `mcpp-ide/i18n/bundle`.

## Responsibilities and implementation

The CLion adapter owns installation lookup and protocol acceptance. It obtains its directory
from the IDE plugin descriptor and retains the existing backend preference: explicit setting,
bundle, xlings, PATH. The initializer rejects incompatible or missing protocol values before
sending any custom requests. No legacy method aliases are introduced.

Startup errors previously reached an action's notification handler without a corresponding
log entry. Startup failures now write a warning with backend source, negotiated protocol,
and, for RPC failures, wire method and error code. Process launch errors and a missing backend
also produce a warning. The existing startup line continues to identify the selected command
locally; private diagnostics are not published as CI artifacts.

The mcppls language service is a separate connection. Its successful initialization in the
user log does not validate the mcpp-ide session, and this lookup failure does not require a
clangd rebuild or a runtime dependency release.

## Controlled local result

The original input is the Marketplace 0.2.0 ZIP, SHA-256
`7e3fec81186d897e4185413ffbf3b69b08f9185f0da78842f4826f2408d274ba`.
Only the plugin JAR was changed for a diagnostic comparison. The backend bundle and every
runner remained byte-identical. With the descriptor lookup and handshake diagnostics, the
same CLion build completes protocol 2 startup using backend 0.2.0, loads translations and
commands, opens the mcpp tool window, and creates all three views.

This patched ZIP is an experiment, not a published release or the canonical Gradle package.
The normal CI build must still compile and package the complete plugin and run that same
candidate ZIP on both stable versions.

## Runtime coverage and limits

`clion-stable.yml` pins CLion 2026.1.5.2 and 2026.2.4.1. Each test starts the actual IDE,
installs the same packaged plugin ZIP with mcppls 0.0.19, opens a synthetic mcpp project,
checks the actual IDE version and build, completes the session, and verifies the visible
tool window and three views. The standalone test build does not compile the product plugin
against each tested SDK. A manual release check can reuse the published ZIP without rebuilding
the backend. Only a report of public versions and assertions is uploaded.

Plugin Verifier remains a separate static compatibility check. These runtime checks cover
Linux startup and views on the two stable IDE families; native Windows/macOS GUI behavior,
dynamic uninstall/reinstall, debugging and every project action require their own evidence.
The null CodeSource reproduction uses a cold IDE start and does not depend on dynamic installation.
