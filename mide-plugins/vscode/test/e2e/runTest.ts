// The extension in a real VS Code: the staged extension (dist/extension, what `vsce package` packs),
// a workspace holding one project, the backend this repository built, and the mock mcpp answering
// it from the golden fixtures. MCPP_E2E_CODE runs an installed VS Code instead of downloading one.
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

import { runTests } from "@vscode/test-electron";

import { built, extensionDir } from "../unit/harness";

async function main(): Promise<void> {
  const backend = built("mcpp-ide"), mock = built("mcpp-ide-mock-mcpp");
  if (!backend || !mock) throw new Error("build the backend first (mcpp build)");
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-e2e-"));
  const workspace = path.join(temp, "app");
  fs.mkdirSync(path.join(workspace, "src"), { recursive: true });
  fs.mkdirSync(path.join(workspace, "tests"), { recursive: true });
  fs.writeFileSync(path.join(workspace, "mcpp.toml"), '[package]\nname = "app"\nversion = "0.1.0"\n');
  fs.writeFileSync(path.join(workspace, "src", "main.cpp"), "int main() { return 0; }\n");
  fs.writeFileSync(path.join(workspace, "tests", "test_smoke.cpp"), "int main() { return 0; }\n");
  const user = path.join(temp, "user-data", "User");
  fs.mkdirSync(user, { recursive: true });
  // MCPP_E2E_BUNDLED: the backend the staged extension carries (backend/mcpp-ide.okb, run by the
  // okrun it carries for this system), as a user gets it.
  fs.writeFileSync(path.join(user, "settings.json"), JSON.stringify({
    ...(process.env.MCPP_E2E_BUNDLED ? {} : { "mcpp.ide.backendPath": backend }),
    "mcpp.path": mock,
    "security.workspace.trust.enabled": false,
  }));
  const executable = process.env.MCPP_E2E_CODE;
  // MCPP_E2E_EXTENSION: another extension directory (an unpacked .vsix) in place of the staged one.
  const extensionDevelopmentPath = process.env.MCPP_E2E_EXTENSION ?? path.join(extensionDir, "dist", "extension");
  await runTests({
    ...(executable ? { vscodeExecutablePath: executable } : { version: "1.91.0" }),
    extensionDevelopmentPath,
    extensionTestsPath: path.join(extensionDir, "out", "test", "e2e", "suite", "index.js"),
    launchArgs: [workspace, "--user-data-dir", path.join(temp, "user-data"), "--extensions-dir", path.join(temp, "extensions"),
                 "--disable-gpu", "--disable-workspace-trust", "--skip-welcome", "--skip-release-notes",
                 // No display (a container, an agent's shell): Chromium's headless platform.
                 ...(process.env.MCPP_E2E_HEADLESS ? ["--ozone-platform=headless"] : [])],
    extensionTestsEnv: { MCPPIDE_E2E_WORKSPACE: workspace, MCPPIDE_E2E_SOURCE: process.env.MCPP_E2E_BUNDLED ? "bundle" : "setting" },
  });
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
