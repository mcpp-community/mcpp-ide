import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import * as vscode from "vscode";

import { contributedCommandIds } from "../../../src/commands/ids";

type Stub = "full" | "partial" | "renamed" | "noapi" | "throwing";

const STUB = (process.env.MCPP_E2E_STUB ?? "full") as Stub;

async function waitForFile(file: string, timeoutMs: number, expectedLine?: string): Promise<string> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (existsSync(file)) {
      const content = readFileSync(file, "utf8");
      if (content.trim().length > 0 && (expectedLine === undefined || content.trim().split(/\r?\n/).includes(expectedLine))) {
        return content;
      }
    }
    await new Promise((resolvePromise) => setTimeout(resolvePromise, 100));
  }
  return existsSync(file) ? readFileSync(file, "utf8") : "";
}

function lines(file: string): string[] {
  return readFileSync(file, "utf8").trim().split("\n").filter((line) => line.length > 0);
}

suite(`mcpp extension smoke (mcppls stub: ${STUB})`, () => {
  test("activates and registers the whole command surface", async () => {
    const extension = vscode.extensions.getExtension("mcpp-community.mcpp-vscode");
    assert.ok(extension, "the mcpp extension should be installed in the development host");
    await extension.activate();

    const languageServer = vscode.extensions.getExtension("sunrisepeak.mcpp-language-server");
    assert.ok(languageServer, "the C++ Modules dependency should be installed next to the extension");
    await languageServer.activate();

    const registered = await vscode.commands.getCommands(true);
    const missing = contributedCommandIds().filter((id) => !registered.includes(id));
    assert.deepEqual(missing, [], "every contributed command must be registered");
  });

  test("contributes the view container, its views, and the theme colours", () => {
    const manifest = vscode.extensions.getExtension("mcpp-community.mcpp-vscode")?.packageJSON as {
      contributes?: Record<string, unknown>;
    };
    const contributes = manifest.contributes ?? {};
    assert.ok(contributes.viewsContainers, "an activity bar container is contributed");
    assert.ok(contributes.views, "views are contributed");
    assert.ok(contributes.colors, "theme colours are contributed");
  });

  test("a build runs mcpp; the language service is asked to reload only when the setting says so", async () => {
    const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
    assert.ok(workspaceFolder, "the fixture workspace should be open");
    const fakeMcpp = process.env.MCPP_E2E_FAKE_MCPP;
    const mcppLog = process.env.MCPP_E2E_LOG;
    const mcpplsLog = process.env.MCPP_E2E_MCPPLS_LOG;
    assert.ok(fakeMcpp, "MCPP_E2E_FAKE_MCPP");
    assert.ok(mcppLog, "MCPP_E2E_LOG");
    assert.ok(mcpplsLog, "MCPP_E2E_MCPPLS_LOG");
    const settings = vscode.workspace.getConfiguration("mcpp", workspaceFolder.uri);
    await settings.update("path", fakeMcpp, vscode.ConfigurationTarget.Workspace);
    const builds = (): number => (existsSync(mcppLog) ? lines(mcppLog).filter((line) => line === "build").length : 0);
    const waitForBuilds = async (count: number): Promise<void> => {
      const deadline = Date.now() + 20_000;
      while (builds() < count && Date.now() < deadline) await new Promise((r) => setTimeout(r, 100));
      assert.ok(builds() >= count, `mcpp build ran ${builds()} time(s), expected ${count}`);
    };

    // `auto`: mcppls reads mcpp's result record and refreshes itself, so nothing is forwarded
    // (design 2026-10-30 part 2 §5.4).
    void vscode.commands.executeCommand("mcpp.build");
    await waitForBuilds(1);
    await new Promise((r) => setTimeout(r, 1500));
    const afterAuto = existsSync(mcpplsLog) ? lines(mcpplsLog) : [];
    assert.ok(!afterAuto.includes("mcppls.reloadBuildDescription"), `auto forwarded: ${afterAuto.join(",")}`);

    // `reload`: the action mcppls names for VS Code, when its extension registered the command.
    await settings.update("languageService.refreshAfterBuild", "reload", vscode.ConfigurationTarget.Workspace);
    void vscode.commands.executeCommand("mcpp.build");
    await waitForBuilds(2);
    if (STUB === "full" || STUB === "noapi" || STUB === "throwing") {
      const recorded = await waitForFile(mcpplsLog, 20_000, "mcppls.reloadBuildDescription");
      assert.ok(recorded.includes("mcppls.reloadBuildDescription"), `expected the reload, saw: ${recorded.trim()}`);
    } else {
      await new Promise((r) => setTimeout(r, 1500));
      const recorded = existsSync(mcpplsLog) ? lines(mcpplsLog) : [];
      assert.deepEqual(recorded.filter((line) => line.startsWith("mcppls.")), [], `${STUB} registered no reload`);
    }
    await settings.update("languageService.refreshAfterBuild", undefined, vscode.ConfigurationTarget.Workspace);
  });

  test("forwarded commands reach mcppls, and a missing one does not throw", async () => {
    const mcpplsLog = process.env.MCPP_E2E_MCPPLS_LOG;
    assert.ok(mcpplsLog);
    const before = existsSync(mcpplsLog) ? lines(mcpplsLog).length : 0;

    // `executeCommand` rejects when a command was never registered, so awaiting
    // these is the assertion: the bridge must swallow a missing capability.
    await vscode.commands.executeCommand("mcpp.languageServer.refreshState");
    await vscode.commands.executeCommand("mcpp.languageServer.showModuleGraph");
    await vscode.commands.executeCommand("mcpp.languageServer.collectReport");

    const after = existsSync(mcpplsLog) ? lines(mcpplsLog) : [];
    const forwarded = after.slice(before);
    if (STUB === "full" || STUB === "noapi" || STUB === "throwing") {
      assert.ok(forwarded.includes("mcppls.showModuleGraph"), `${STUB}: ${forwarded.join(",")}`);
      assert.ok(forwarded.includes("mcppls.collectReport"), `${STUB}: ${forwarded.join(",")}`);
    } else {
      assert.deepEqual(forwarded, [], `${STUB} should have no such command to call`);
    }
  });

  test("the legacy 0.4.x ids still work", async () => {
    const mcpplsLog = process.env.MCPP_E2E_MCPPLS_LOG;
    assert.ok(mcpplsLog);
    const before = existsSync(mcpplsLog) ? lines(mcpplsLog).length : 0;
    await vscode.commands.executeCommand("mcpp.configureClangd");
    await vscode.commands.executeCommand("mcpp.showLanguageServerLogs");
    const forwarded = (existsSync(mcpplsLog) ? lines(mcpplsLog) : []).slice(before);
    if (STUB === "full" || STUB === "noapi" || STUB === "throwing") {
      assert.ok(forwarded.includes("mcppls.selectContext"), forwarded.join(","));
      assert.ok(forwarded.includes("mcppls.showLogs"), forwarded.join(","));
    }
  });

  test("reading mcppls state never throws, whatever the stub exposes", async () => {
    // Whether `exports` is a conforming object, nothing at all, or one that throws,
    // the self-check has to produce a snapshot rather than an error.
    await vscode.commands.executeCommand("mcpp.selfCheck");
  });

  test("the backend starts and reaches mcpp through the configured path", async () => {
    const extension = vscode.extensions.getExtension<{ session: { request<T>(method: string, params?: unknown): Promise<T> } }>(
      "mcpp-community.mcpp-vscode");
    assert.ok(extension);
    const api = await extension.activate();
    const exec = await api.session.request<{ exitCode: number; stdout: string }>("mcpp/exec", { args: ["--version"] });
    assert.equal(exec.exitCode, 0);
    assert.match(exec.stdout, /^mcpp \d/);
    const mcppLog = process.env.MCPP_E2E_LOG;
    assert.ok(mcppLog);
    assert.ok(lines(mcppLog).includes("--version"), "the backend ran the configured mcpp");
  });

  test("the targets come from the backend, with no set while mcpp names none", async () => {
    const api = await vscode.extensions.getExtension<{ session: { request<T>(method: string, params?: unknown): Promise<T> } }>(
      "mcpp-community.mcpp-vscode")!.activate();
    const targets = await api.session.request<{ rows: { target: string; set?: boolean }[] }>("targets/list", {});
    assert.ok(targets.rows.some((row) => row.target === "x86_64-openkal"));
    // The mock is mcpp 2026.10.27.1, before target sets (2026.10.30.1).
    assert.ok(!targets.rows.some((row) => row.set === true));
  });

  test("an mcpp launch configuration without a debugger extension says so and starts nothing", async () => {
    const folder = vscode.workspace.workspaceFolders?.[0];
    const started = await vscode.debug.startDebugging(folder, { type: "mcpp", request: "launch", name: "mcpp: debug" });
    assert.equal(started, false);
  });

  test("mcpp.showCachePanel brings the cache view on screen", async () => {
    await vscode.commands.executeCommand("mcpp.showCachePanel");
  });

  test("opening the settings panel does not throw", async () => {
    await vscode.commands.executeCommand("mcpp.openSettings");
  });

  test("build.mcpp opens as cpp, and C++ Modules is left the names it answers", async () => {
    const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
    assert.ok(workspaceFolder, "the fixture workspace should be open");
    const uri = vscode.Uri.joinPath(workspaceFolder.uri, "build.mcpp");
    const document = await vscode.workspace.openTextDocument(uri);
    assert.equal(document.languageId, "cpp", "build.mcpp must reach the C++ language service");

    // The stub is installed and enabled, so the rule checks run and `unknownSymbol` does not.
    const deadline = Date.now() + 10_000;
    let codes: string[] = [];
    while (Date.now() < deadline) {
      codes = vscode.languages.getDiagnostics(uri).map((diagnostic) => String(diagnostic.code));
      if (codes.length > 0) {
        break;
      }
      await new Promise((resolvePromise) => setTimeout(resolvePromise, 100));
    }
    assert.deepEqual(codes, ["mcpp.buildscript.rpathInLinkFlag"]);
  });
});
