import { chmodSync, copyFileSync, existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";

import { runTests } from "@vscode/test-electron";

import { STUB_VARIANTS, stubExtensionJs, stubPackageJson, stubServerJs, type StubVariant } from "./mcpplsStub";

/**
 * One Extension Host run per mcppls variant. Each run gets its own extensions
 * directory and user data, so a variant cannot leak into the next one, and the
 * only thing that differs between them is which commands the stub offers.
 */
async function runVariant(repositoryRoot: string, fixtureRoot: string, variant: StubVariant): Promise<void> {
  const tempRoot = mkdtempSync(join(tmpdir(), `mcpp-vscode-e2e-${variant}-`));
  const userDataDir = join(tempRoot, "user-data");
  const extensionsDir = join(tempRoot, "extensions");
  const workspaceDir = join(tempRoot, "project");
  // Windows cannot execFile() a shebang script, so there the fake masquerades as
  // an npm-style .cmd shim (which src/cli/process.ts routes through the shell)
  // that forwards to the plain node script sitting next to it. Elsewhere the
  // script with its shebang is the executable.
  const fakeMcppScript = join(tempRoot, "fake-mcpp.js");
  const fakeMcpp = process.platform === "win32" ? join(tempRoot, "fake-mcp.cmd") : join(tempRoot, "mcpp");
  const mcppLogPath = join(tempRoot, "mcpp.log");
  const mcpplsLogPath = join(tempRoot, "mcppls.log");
  const mcpplsStub = join(extensionsDir, "sunrisepeak.mcpp-language-server-0.0.0");

  mkdirSync(userDataDir, { recursive: true });
  mkdirSync(extensionsDir, { recursive: true });
  mkdirSync(workspaceDir, { recursive: true });
  mkdirSync(mcpplsStub, { recursive: true });
  copyFileSync(join(fixtureRoot, "fake-mcpp.js"), fakeMcppScript);
  if (process.platform === "win32") {
    writeFileSync(fakeMcpp, `@node "%~dp0fake-mcpp.js" %*\r\n`);
  } else {
    copyFileSync(fakeMcppScript, fakeMcpp);
    chmodSync(fakeMcpp, 0o755);
  }
  writeFileSync(join(mcpplsStub, "package.json"), stubPackageJson(variant));
  writeFileSync(join(mcpplsStub, "extension.js"), stubExtensionJs(variant));
  // The stub's server, in its payload as the real extension carries it: the backend asks it for
  // mcppls's actions. A node script, behind a .cmd shim on Windows as the fake mcpp is.
  const payloadBin = join(mcpplsStub, "payload", "bin");
  mkdirSync(payloadBin, { recursive: true });
  writeFileSync(join(payloadBin, "mcppls.js"), stubServerJs(variant));
  const server = process.platform === "win32" ? "mcppls.cmd" : "mcppls";
  if (process.platform === "win32") {
    writeFileSync(join(payloadBin, server), `@node "%~dp0mcppls.js" %*\r\n`);
  } else {
    writeFileSync(join(payloadBin, server), `#!/usr/bin/env node\n${stubServerJs(variant)}`);
    chmodSync(join(payloadBin, server), 0o755);
  }
  writeFileSync(join(mcpplsStub, "payload", "payload.json"), JSON.stringify({ server: { path: `bin/${server}` } }));
  copyFileSync(join(fixtureRoot, "project/mcpp.toml"), join(workspaceDir, "mcpp.toml"));
  copyFileSync(join(fixtureRoot, "project/main.cpp"), join(workspaceDir, "main.cpp"));
  copyFileSync(join(fixtureRoot, "project/build.mcpp"), join(workspaceDir, "build.mcpp"));
  // The backend: the one this build produced when MCPP_IDE_BACKEND names it, as a user's settings
  // would, else the bundle staged into the extension (npm run stage:backend), as a user's
  // installation carries it. And the fake mcpp from the start: the backend reaches mcpp through it.
  const backend = process.env.MCPP_IDE_BACKEND;
  if (!backend && !existsSync(join(repositoryRoot, "backend", "mcpp-ide.okb"))) {
    throw new Error("no backend: set MCPP_IDE_BACKEND, or stage the bundle (npm run stage:backend)");
  }
  mkdirSync(join(userDataDir, "User"), { recursive: true });
  writeFileSync(join(userDataDir, "User", "settings.json"), JSON.stringify({
    ...(backend ? { "mcpp.ide.backendPath": backend } : {}),
    "mcpp.path": fakeMcpp,
  }, null, 2));

  // `MCPP_E2E_CODE` runs the suite against an already-installed VS Code, which is
  // what makes the Extension Host tests runnable without the 135 MB download.
  // CI leaves it unset and pins the version instead.
  const executable = process.env.MCPP_E2E_CODE ?? "";
  const launcher = executable.length === 0
    ? { version: "1.91.0" }
    : { vscodeExecutablePath: executable };

  try {
    await runTests({
      ...launcher,
      extensionDevelopmentPath: repositoryRoot,
      extensionTestsPath: join(repositoryRoot, "dist/test/e2e/suite/index.js"),
      launchArgs: [
        workspaceDir,
        "--user-data-dir",
        userDataDir,
        "--extensions-dir",
        extensionsDir,
        "--disable-updates",
        "--skip-welcome",
        "--disable-workspace-trust",
      ],
      extensionTestsEnv: {
        MCPP_E2E_FAKE_MCPP: fakeMcpp,
        MCPP_E2E_FIXTURES: join(repositoryRoot, "..", "..", "protocol", "fixtures"),
        MCPP_E2E_LOG: mcppLogPath,
        MCPP_E2E_MCPPLS_LOG: mcpplsLogPath,
        MCPP_E2E_STUB: variant,
      },
    });
  } finally {
    rmSync(tempRoot, { recursive: true, force: true });
  }
}

async function main(): Promise<void> {
  const repositoryRoot = resolve(__dirname, "../../..");
  const fixtureRoot = join(repositoryRoot, "test/e2e/fixtures");
  // `MCPP_E2E_STUB=partial` runs one variant; unset runs them all.
  const requested = process.env.MCPP_E2E_STUB;
  const variants = requested === undefined
    ? STUB_VARIANTS
    : STUB_VARIANTS.filter((variant) => variant === requested);

  if (variants.length === 0) {
    throw new Error(`MCPP_E2E_STUB=${requested} is not one of ${STUB_VARIANTS.join(", ")}`);
  }

  for (const variant of variants) {
    console.log(`e2e: running the extension host against the "${variant}" mcppls stub`);
    await runVariant(repositoryRoot, fixtureRoot, variant);
  }
}

void main().catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
