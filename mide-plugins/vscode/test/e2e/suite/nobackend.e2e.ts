// The declared degraded mode (mcpp design 2026-10-30 part 2 §8, F15): the extension carries the
// backend, and okrun runs on neither this system nor PATH, as on macOS x86_64, Windows arm64 or Linux
// armhf. The extension says so, does not try to start the backend again, and build, run, test and
// the project pages go on: a build runs mcpp, and mcpp's queries run here rather than through the
// backend.
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import * as vscode from "vscode";

async function waitFor(check: () => boolean, timeoutMs: number): Promise<boolean> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (check()) return true;
    await new Promise((r) => setTimeout(r, 100));
  }
  return check();
}

suite("mcpp extension without its backend (the declared degraded mode)", () => {
  test("the extension activates in the degraded mode, and says so", async () => {
    const extension = vscode.extensions.getExtension("mcpp-community.mcpp-vscode");
    assert.ok(extension);
    const api = (await extension.activate()) as { session: { degraded: boolean; running: boolean; request(method: string): Promise<unknown> } };
    assert.ok(await waitFor(() => api.session.degraded, 20_000), "the session is degraded");
    assert.equal(api.session.running, false);
    await assert.rejects(api.session.request("selfCheck"), /not running/);
  });

  test("a build runs mcpp, and mcpp's queries run without the backend", async () => {
    const folder = vscode.workspace.workspaceFolders?.[0];
    assert.ok(folder);
    const fakeMcpp = process.env.MCPP_E2E_FAKE_MCPP, mcppLog = process.env.MCPP_E2E_LOG;
    assert.ok(fakeMcpp && mcppLog);
    await vscode.workspace.getConfiguration("mcpp", folder.uri).update("path", fakeMcpp, vscode.ConfigurationTarget.Workspace);
    void vscode.commands.executeCommand("mcpp.build");
    const lines = () => (existsSync(mcppLog) ? readFileSync(mcppLog, "utf8").split(/\r?\n/) : []);
    assert.ok(await waitFor(() => lines().includes("build"), 20_000), `mcpp build did not run: ${lines().join(",")}`);
    // The self-check asks mcpp for its version and protocol (0.9.0's parsers, mcpp run here).
    void vscode.commands.executeCommand("mcpp.selfCheck");
    assert.ok(await waitFor(() => lines().some((l) => l.startsWith("--version") || l.startsWith("--protocol-version")), 20_000),
      `no query reached mcpp: ${lines().join(",")}`);
  });
});
