// The shared panels (webui/) in VS Code: editor-area panels and the sidebar's webview views. The
// panel speaks the backend's protocol through this host (rpc in, notifications out) and asks it for
// what only the editor does: open a file, confirm, write a setting, run a command.
import { randomBytes } from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";

import { MCPPLS_EXTENSION, panelTitle } from "./mapping";
import type { Session } from "./session";

export interface PanelParams {
  root?: string;
  graph?: string;
  id?: string;
  focus?: string;
}

type HostAction = (params: any) => Promise<unknown> | unknown;

export class Panels implements vscode.Disposable {
  private readonly open = new Map<string, vscode.WebviewPanel>();
  private readonly webviews = new Set<vscode.Webview>();
  private readonly disposables: vscode.Disposable[] = [];
  private readonly webuiRoot: vscode.Uri;

  constructor(private readonly context: vscode.ExtensionContext, private readonly session: Session,
              private readonly runCommand: (command: string, params: any) => Promise<unknown>) {
    this.webuiRoot = vscode.Uri.joinPath(context.extensionUri, "webui");
    // Every notification of the backend reaches every panel; each panel takes what it shows.
    this.disposables.push(session.on("*", ({ method, params }: { method: string; params: unknown }) => {
      for (const w of this.webviews) void w.postMessage({ type: "notify", method, params });
    }));
    for (const [view, panel] of [["mcpp.library", "libraries"], ["mcpp.cache", "cache"]] as const)
      this.disposables.push(vscode.window.registerWebviewViewProvider(view, {
        resolveWebviewView: (v) => { const forget = this.attach(v.webview, panel, () => this.paramsForView()); v.onDidDispose(forget); },
      }));
  }

  private paramsForView(): PanelParams {
    const editor = vscode.window.activeTextEditor;
    const projects = this.session.info?.projects ?? [];
    const root = editor ? projects.find((p) => editor.document.uri.fsPath.startsWith(p.root))?.root : undefined;
    return { root: root ?? projects[0]?.root };
  }

  /** Opens (or reveals) a panel in the editor area. */
  show(panel: string, params: PanelParams = {}): void {
    const key = `${panel}:${params.root ?? ""}:${params.graph ?? ""}:${params.id ?? ""}`;
    const existing = this.open.get(key);
    if (existing) { existing.reveal(); return; }
    const titles: Record<string, string> = {
      project: this.session.t("Project"), libraries: this.session.t("Libraries"), library: params.id ?? this.session.t("Libraries"),
      cache: this.session.t("Cache"), settings: this.session.t("Settings"), pack: this.session.t("Pack"),
      targets: this.session.t("Targets and Platforms"),
      graph: params.graph === "modules" ? this.session.t("Module graph") : this.session.t("Dependency graph"),
    };
    const p = vscode.window.createWebviewPanel(`mcpp.${panel}`, panelTitle(titles[panel] ?? panel), vscode.ViewColumn.Active,
      { enableScripts: true, retainContextWhenHidden: true, localResourceRoots: [this.webuiRoot] });
    p.iconPath = vscode.Uri.joinPath(this.context.extensionUri, "media", "logo.png");
    this.open.set(key, p);
    const forget = this.attach(p.webview, panel === "library" ? "libraries" : panel, () => params);
    p.onDidDispose(() => { this.open.delete(key); forget(); });
  }

  /** Loads the panel into a webview; the returned function forgets it when the webview ends. */
  private attach(webview: vscode.Webview, panel: string, params: () => PanelParams): () => void {
    webview.options = { enableScripts: true, localResourceRoots: [this.webuiRoot] };
    const nonce = randomBytes(16).toString("base64");
    const base = `${webview.asWebviewUri(this.webuiRoot).toString()}/`;
    const csp = `default-src 'none'; img-src ${webview.cspSource} data:; style-src ${webview.cspSource} 'unsafe-inline'; script-src 'nonce-${nonce}' ${webview.cspSource};`;
    const html = fs.readFileSync(path.join(this.webuiRoot.fsPath, "index.html"), "utf8")
      .split("{{base}}").join(base).split("{{csp}}").join(csp).split("{{nonce}}").join(nonce)
      .split("{{lang}}").join(vscode.env.language);
    this.webviews.add(webview);
    const sub = webview.onDidReceiveMessage((m) => this.receive(webview, panel, params, m));
    webview.html = html;
    return () => { this.webviews.delete(webview); sub.dispose(); };
  }

  private async receive(webview: vscode.Webview, panel: string, params: () => PanelParams, m: any): Promise<void> {
    if (m?.type === "ready") {
      await webview.postMessage({ type: "init", panel, params: params(), bundle: this.session.strings, editor: "vscode" });
      return;
    }
    if (m?.type === "rpc") {
      try {
        const result = await this.session.request(m.method, m.params);
        await webview.postMessage({ type: "rpcResult", id: m.id, result });
      } catch (e) {
        const error = (e as { error?: unknown }).error ?? { code: -32603, message: e instanceof Error ? e.message : String(e) };
        await webview.postMessage({ type: "rpcError", id: m.id, error });
      }
      return;
    }
    if (m?.type === "host") {
      const action = this.actions()[m.action];
      try {
        if (!action) throw new Error(`unknown host action ${m.action}`);
        const result = await action(m.params ?? {});
        await webview.postMessage({ type: "hostResult", id: m.id, result: result ?? null });
      } catch (e) {
        await webview.postMessage({ type: "hostError", id: m.id, error: { message: e instanceof Error ? e.message : String(e) } });
      }
    }
  }

  private actions(): Record<string, HostAction> {
    const t = (k: string, ...a: unknown[]) => this.session.t(k, ...a);
    const target = (key: string) => {
      const i = vscode.workspace.getConfiguration().inspect(key);
      if (i?.workspaceFolderValue !== undefined) return vscode.ConfigurationTarget.WorkspaceFolder;
      if (i?.workspaceValue !== undefined) return vscode.ConfigurationTarget.Workspace;
      return vscode.ConfigurationTarget.Global;
    };
    return {
      settings: () => this.session.settings(),
      setSetting: async (p) => { await vscode.workspace.getConfiguration().update(p.key, p.value, target(p.key)); return true; },
      resetSettings: async (p) => {
        for (const key of p.keys as string[]) await vscode.workspace.getConfiguration().update(key, undefined, target(key));
        return true;
      },
      migrateSettings: async (p) => {
        for (const r of p.renames as { from: string; to: string }[]) {
          const i = vscode.workspace.getConfiguration().inspect(r.from);
          if (i?.globalValue !== undefined) await vscode.workspace.getConfiguration().update(r.to, i.globalValue, vscode.ConfigurationTarget.Global);
          if (i?.workspaceValue !== undefined) await vscode.workspace.getConfiguration().update(r.to, i.workspaceValue, vscode.ConfigurationTarget.Workspace);
        }
        return true;
      },
      openFile: async (p) => {
        const doc = await vscode.workspace.openTextDocument(vscode.Uri.file(p.path));
        const line = Math.max(0, (p.line ?? 1) - 1);
        await vscode.window.showTextDocument(doc, { selection: new vscode.Range(line, 0, line, 0) });
        return true;
      },
      openExternal: (p) => vscode.env.openExternal(vscode.Uri.parse(p.url)),
      reveal: (p) => vscode.commands.executeCommand("revealFileInOS", vscode.Uri.file(p.path)),
      copy: async (p) => { await vscode.env.clipboard.writeText(p.text); void vscode.window.showInformationMessage(t("Copied.")); return true; },
      notify: (p) => (p.level === "error" ? vscode.window.showErrorMessage(p.message) : p.level === "warning" ? vscode.window.showWarningMessage(p.message) : vscode.window.showInformationMessage(p.message)),
      openPanel: (p) => { const { panel, ...rest } = p; this.show(panel, rest); return true; },
      runCommand: (p) => this.runCommand(p.command, p),
      confirm: async (p) => {
        const yes = t("Continue");
        const detail = [p.detail, p.preview].filter(Boolean).join("\n\n");
        const choice = await vscode.window.showWarningMessage(p.title, { modal: true, detail }, yes);
        if (choice !== yes) return { confirmed: false };
        if (!p.acknowledge) return { confirmed: true };
        const ack = t("I understand this affects every mcpp project on this machine");
        const second = await vscode.window.showWarningMessage(p.title, { modal: true, detail: ack }, ack);
        return { confirmed: second === ack, acknowledged: second === ack };
      },
      languageService: async (p) => {
        if (p.action === "install") await vscode.commands.executeCommand("workbench.extensions.installExtension", MCPPLS_EXTENSION);
        else await vscode.commands.executeCommand("workbench.extensions.search", `@id:${MCPPLS_EXTENSION}`);
        return true;
      },
    };
  }

  dispose(): void {
    for (const p of this.open.values()) p.dispose();
    this.disposables.forEach((d) => d.dispose());
  }
}
