// The commands: every id the extension declares (generated from spec/commands.json) is registered
// at activation and does what its row's `action` says. The adapter maps an action to VS Code's UI;
// which argv runs, what a cleanup costs and what a menu offers are the backend's answers.
import * as vscode from "vscode";

import type { Debugging } from "./debug";
import type { Jobs } from "./jobs";
import type { Panels } from "./panels";
import type { Projects } from "./projects";
import type { CleanPlan, CommandEntry, LanguageServiceStatus, TargetRow } from "./protocol";
import { runProgram } from "./run";
import type { Session } from "./session";
import type { Tasks } from "./tasks";

export interface CommandContext {
  session: Session;
  projects: Projects;
  jobs: Jobs;
  tasks: Tasks;
  panels: Panels;
  debugging: Debugging;
}

/** plan -> the editor's confirmation at the plan's level -> apply. */
export async function cleanup(c: CommandContext, action: string): Promise<void> {
  const root = c.projects.project?.root;
  const t = (k: string, ...a: unknown[]) => c.session.t(k, ...a);
  const plan = await c.session.request<CleanPlan>("cache/plan", { action, root });
  if (plan.refusedUntrusted) { void vscode.window.showWarningMessage(t("this action needs a trusted workspace")); return; }
  const confirmation: Record<string, boolean> = {};
  if (plan.level > 0) {
    let preview = "";
    if (typeof plan.preview === "object" && plan.preview.source === "mcpp") preview = plan.preview.text ?? "";
    else if (typeof plan.preview === "object") preview = t("About {0} would be freed ({1} entries).", plan.preview.bytesText, plan.preview.count);
    const yes = t("Continue");
    const choice = await vscode.window.showWarningMessage(plan.title, { modal: true, detail: [plan.detail, preview].filter(Boolean).join("\n\n") }, yes);
    if (choice !== yes) return;
    confirmation.confirmed = true;
    confirmation.previewShown = !!plan.preview;
    if (plan.acknowledge) {
      const ack = t("I understand this affects every mcpp project on this machine");
      if ((await vscode.window.showWarningMessage(plan.title, { modal: true, detail: ack }, ack)) !== ack) return;
      confirmation.acknowledged = true;
    }
  }
  await c.jobs.start({ action, root, confirmation }, {
    output: (text) => c.session.output.append(text),
    finished: (p) => {
      if (p.status === "ok") void vscode.window.showInformationMessage(t("mcpp: {0}", plan.title.replace(/\?$/, "")));
      else void vscode.window.showErrorMessage(t("mcpp {0}: {1}", plan.argv.join(" "), p.status));
    },
  }, "cache/apply");
}

async function selectTarget(c: CommandContext): Promise<void> {
  const t = (k: string, ...a: unknown[]) => c.session.t(k, ...a);
  const r = await c.session.request<{ host: string; rows: TargetRow[] }>("targets/list", { root: c.projects.project?.root });
  const pick = await vscode.window.showQuickPick(
    r.rows.map((row) => ({
      label: `${row.selected ? "$(check) " : ""}${row.target}`,
      description: [row.host ? t("this machine") : "", row.platformText].filter(Boolean).join(" · "),
      detail: row.toolchains.map((tc) => `${tc.spec || "—"} (${tc.status})`).join(", ") || undefined,
      row,
    })),
    { title: t("Select the build target"), placeHolder: t("build, run, test, debug and pack use it") });
  if (!pick) return;
  await vscode.workspace.getConfiguration().update("mcpp.build.target", pick.row.host ? "" : pick.row.target, vscode.ConfigurationTarget.Workspace);
  if (!pick.row.installed && pick.row.pin) {
    const install = t("Install");
    if ((await vscode.window.showInformationMessage(t("No toolchain is installed for {0}.", pick.row.target), install)) === install)
      await c.jobs.start({ spec: pick.row.pin, target: pick.row.target }, { output: (x) => c.session.output.append(x) }, "toolchains/install");
  }
}

async function installToolchain(c: CommandContext): Promise<void> {
  const spec = await vscode.window.showInputBox({ title: c.session.t("Install an mcpp toolchain"), placeHolder: "gcc@16.1.0, llvm@23.1.3" });
  if (!spec) return;
  c.session.output.show(true);
  await c.jobs.start({ spec }, { output: (x) => c.session.output.append(x) }, "toolchains/install");
}

async function selectDefaultToolchain(c: CommandContext): Promise<void> {
  const r = await c.session.request<{ rows: { target: string; spec: string; status: string; default: boolean }[]; host: string }>("toolchains/list", {});
  const rows = r.rows.filter((x) => x.target === r.host && x.spec);
  const pick = await vscode.window.showQuickPick(rows.map((x) => ({ label: `${x.default ? "$(check) " : ""}${x.spec}`, description: x.status, spec: x.spec })),
    { title: c.session.t("Choose the mcpp global default toolchain") });
  if (!pick) return;
  await c.session.request("toolchains/setDefault", { spec: pick.spec });
}

async function newProject(c: CommandContext): Promise<void> {
  const t = (k: string, ...a: unknown[]) => c.session.t(k, ...a);
  const folder = await vscode.window.showOpenDialog({ canSelectFolders: true, canSelectFiles: false, openLabel: t("Create the project here") });
  if (!folder?.[0]) return;
  const name = await vscode.window.showInputBox({ title: t("New mcpp project (1/2)"), prompt: t("The project's name, which is also its directory") });
  if (!name) return;
  const handle = await c.jobs.start({ parent: folder[0].fsPath, name }, { output: (x) => c.session.output.append(x) }, "project/new");
  const done = await handle.done;
  if (done.status !== "ok") { void vscode.window.showErrorMessage(t("mcpp new {0}: {1}", name, done.status)); return; }
  const open = t("Open");
  if ((await vscode.window.showInformationMessage(t("Created {0}.", name), open)) === open)
    await vscode.commands.executeCommand("vscode.openFolder", vscode.Uri.joinPath(folder[0], name), { forceNewWindow: false });
}

async function languageService(c: CommandContext): Promise<void> {
  const s = await c.session.request<LanguageServiceStatus>("languageService/status", { candidates: c.session.mcpplsCandidates() });
  const show = s.state === "ok" ? vscode.window.showInformationMessage : vscode.window.showWarningMessage;
  const choice = await show(s.message, ...s.actions.map((a) => a.title));
  const action = s.actions.find((a) => a.title === choice);
  if (action) await act(action.id);
}

async function act(id: string): Promise<void> {
  if (id === "install") await vscode.commands.executeCommand("workbench.extensions.installExtension", "sunrisepeak.mcpp-language-server");
  else await vscode.commands.executeCommand("workbench.extensions.search", "@id:sunrisepeak.mcpp-language-server");
}

async function selfCheck(c: CommandContext): Promise<void> {
  const r = await c.session.request<{ backend: { version: string }; checks: { status: string; message: string }[] }>("selfCheck", {});
  const out = c.session.output;
  out.appendLine("");
  out.appendLine(`===== mcpp: ${c.session.t("Environment Self-check")} =====`);
  out.appendLine(`mcpp-vscode ${c.session.context.extension.packageJSON.version} · VS Code ${vscode.version} · ${process.platform}-${process.arch}`);
  out.appendLine(`mcpp-ide ${r.backend.version}`);
  for (const check of r.checks) out.appendLine(`${check.status.padEnd(8)} ${check.message}`);
  out.appendLine("========================================");
  out.show(true);
}

async function quickMenu(c: CommandContext): Promise<void> {
  const r = await c.session.request<{ items: { command: string; title: string; group: string; enabled: boolean; reason?: string; detail?: string }[] }>(
    "menu/quick", { root: c.projects.project?.root, editor: "vscode" });
  const pick = await vscode.window.showQuickPick(r.items.map((i) => ({
    label: i.title, description: i.enabled ? i.group : i.reason, detail: i.detail, command: i.command, enabled: i.enabled,
  })), { title: c.session.t("Open the mcpp project and toolchain quick menu") });
  if (pick?.enabled) await vscode.commands.executeCommand(pick.command);
}

async function forward(c: CommandContext, commands: string[], args: unknown[]): Promise<void> {
  const known = await vscode.commands.getCommands(true);
  const command = commands.find((x) => known.includes(x));
  if (!command) {
    await languageService(c);
    return;
  }
  await vscode.commands.executeCommand(command, ...args);
}

export async function dispatch(c: CommandContext, entry: CommandEntry, args: unknown[]): Promise<void> {
  const a = entry.action;
  const root = async () => (await c.projects.require())?.root;
  const options = (args[0] && typeof args[0] === "object" ? args[0] : {}) as { filter?: string; bin?: string; root?: string; args?: { filter?: string } };
  switch (a.kind) {
    case "job": {
      const r = options.root ?? (await root());
      if (!r) return;
      const filter = options.filter ?? options.args?.filter;
      const exec = await c.tasks.execute(a.job === "test" ? "test" : "build", r, filter ? { args: [filter] } : {});
      if (a.then === "languageService") {
        const sub = vscode.tasks.onDidEndTaskProcess((e) => { if (e.execution === exec) { sub.dispose(); void languageService(c); } });
      }
      return;
    }
    case "run": { const r = options.root ?? (await root()); if (r) await runProgram(c.session, r, { bin: options.bin }); return; }
    case "debug": await c.debugging.start(); return;
    case "cache": await cleanup(c, a.plan!); return;
    case "panel": c.panels.show(a.panel === "library" ? "libraries" : a.panel!, { root: c.projects.project?.root, graph: a.graph, id: typeof args[0] === "string" ? args[0] : undefined }); return;
    case "cancel": {
      const ids = c.jobs.runningIds;
      for (const id of ids) await c.session.request("job/cancel", { jobId: id });
      return;
    }
    case "selectTarget": await selectTarget(c); return;
    case "installToolchain": await installToolchain(c); return;
    case "selectDefaultToolchain": await selectDefaultToolchain(c); return;
    case "newProject": await newProject(c); return;
    case "quickMenu": await quickMenu(c); return;
    case "selfCheck": await selfCheck(c); return;
    case "languageService": await languageService(c); return;
    case "forward": await forward(c, a.commands ?? [], args); return;
    case "editor": await vscode.commands.executeCommand(a.vscode!, ...(a.args ?? [])); return;
    case "rpc": {
      const handle = await c.jobs.start({ root: c.projects.project?.root }, { output: (x) => c.session.output.append(x) }, a.method!);
      const done = await handle.done;
      if (done.status !== "ok") void vscode.window.showErrorMessage(c.session.t("mcpp: {0}", done.status));
      return;
    }
  }
}

/** Registers every declared command; the backend's table says what each one does. */
export function registerCommands(context: vscode.ExtensionContext, c: CommandContext): void {
  const declared = (context.extension.packageJSON?.contributes?.commands ?? []) as { command: string }[];
  for (const { command } of declared) {
    context.subscriptions.push(vscode.commands.registerCommand(command, async (...args: unknown[]) => {
      try {
        if (!c.session.commands.length) await c.session.start();
        const entry = c.session.commands.find((x) => x.id === command);
        if (!entry) throw new Error(c.session.t("unknown command {0}", command));
        await dispatch(c, entry, args);
      } catch (e) {
        void vscode.window.showErrorMessage(c.session.t("mcpp: {0}", e instanceof Error ? e.message : String(e)));
      }
    }));
  }
  // Commands the views use and the palette does not list.
  context.subscriptions.push(
    vscode.commands.registerCommand("mcpp.languageServer.act", (id: string) => act(id)),
    vscode.commands.registerCommand("mcpp.openProjectPanel", () => c.panels.show("project", { root: c.projects.project?.root })),
  );
}
