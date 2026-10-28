// The extension host's tests: each case below, in order, failing at the first assertion.
import * as assert from "node:assert/strict";
import * as fs from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";

import type { McppApi } from "../../../src/extension";

const workspace = process.env.MCPPIDE_E2E_WORKSPACE!;
const until = async (what: string, ok: () => boolean | Promise<boolean>, ms = 30_000) => {
  const end = Date.now() + ms;
  while (Date.now() < end) {
    if (await ok()) return;
    await new Promise((r) => setTimeout(r, 100));
  }
  throw new Error(`timed out: ${what}`);
};

const cases: [string, (api: McppApi) => Promise<void>][] = [
  ["every declared command is registered", async () => {
    const declared = vscode.extensions.getExtension("mcpp-community.mcpp-vscode")!.packageJSON.contributes.commands as { command: string }[];
    const known = new Set(await vscode.commands.getCommands(true));
    for (const c of declared) assert.ok(known.has(c.command), c.command);
    assert.ok(declared.length > 40);
  }],
  ["the backend found the project and mcpp", async (api) => {
    // As the run asked: the backend the settings name, or the bundle the extension carries.
    assert.equal(api.session.started?.source, process.env.MCPPIDE_E2E_SOURCE);
    const info = api.session.info!;
    assert.equal(info.projects.length, 1);
    assert.equal(info.projects[0].name, "app");
    assert.equal(info.mcpp?.contract, true);
    await until("the current project", async () => (await api.projects.resolve())?.name === "app");
  }],
  ["a build runs as a task and ends ok", async (api) => {
    const finished = new Promise<{ status: string }>((resolve) => { const d = api.session.on("job/finished", (p) => { d.dispose(); resolve(p); }); });
    await vscode.commands.executeCommand("mcpp.build");
    assert.equal((await finished).status, "ok");
    await until("the task to end", () => vscode.tasks.taskExecutions.length === 0);
  }],
  ["a failed build puts mcpp's diagnostic on the source", async (api) => {
    fs.writeFileSync(path.join(workspace, ".mock-mcpp-fail"), "build");
    const finished = new Promise<{ status: string }>((resolve) => { const d = api.session.on("job/finished", (p) => { d.dispose(); resolve(p); }); });
    await vscode.commands.executeCommand("mcpp.build");
    assert.equal((await finished).status, "failed");
    fs.rmSync(path.join(workspace, ".mock-mcpp-fail"));
    await until("the task to end", () => vscode.tasks.taskExecutions.length === 0);
    await until("a diagnostic", () => vscode.languages.getDiagnostics().some(([uri, ds]) => uri.fsPath.endsWith("main.cpp") && ds.some((d) => d.source === "mcpp")));
  }],
  ["the targets panel opens", async () => {
    await vscode.commands.executeCommand("mcpp.showTargets");
    await until("a panel tab", () => vscode.window.tabGroups.all.some((g) => g.tabs.some((t) => t.label.startsWith("mcpp: "))));
  }],
  ["mcpp.toml is completed from mcpp's key registry", async () => {
    const doc = await vscode.workspace.openTextDocument(path.join(workspace, "mcpp.toml"));
    await vscode.window.showTextDocument(doc);
    assert.equal(doc.languageId, "mcpp-toml");
    const list = await vscode.commands.executeCommand<vscode.CompletionList>("vscode.executeCompletionItemProvider", doc.uri, new vscode.Position(3, 0));
    const labels = list.items.map((i) => (typeof i.label === "string" ? i.label : i.label.label));
    assert.ok(labels.includes("description"), `a key of [package]: ${labels.slice(0, 10).join(", ")}`);
  }],
  ["run starts the program in a terminal", async () => {
    await vscode.commands.executeCommand("mcpp.run");
    await until("the run terminal", () => vscode.window.terminals.some((t) => t.name.startsWith("mcpp run:")));
  }],
];

export async function run(): Promise<void> {
  const ext = vscode.extensions.getExtension<McppApi>("mcpp-community.mcpp-vscode");
  assert.ok(ext, "the extension is installed");
  const api = await ext.activate();
  // The notifications, for the log of a failing run.
  api.session.on("*", ({ method, params }: { method: string; params: { jobId?: string; status?: string } }) => {
    if (method.startsWith("job/started") || method.startsWith("job/finished")) console.log(`  ${method} ${params.jobId ?? ""} ${params.status ?? ""}`);
  });
  for (const [name, fn] of cases) {
    try {
      await fn(api);
      console.log(`ok   ${name}`);
    } catch (e) {
      console.log(`FAIL ${name}`);
      throw e;
    }
  }
}
