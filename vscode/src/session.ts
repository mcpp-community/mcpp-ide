// The session with the backend: started at activation, given the editor's settings, trust,
// language and folders, kept current as they change, and restarted (once, then on request) when it
// ends. Everything else in the adapter reaches the backend through `session.request`.
import * as fs from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";

import { Backend, BackendError } from "./backend/client";
import { locateBackend, type Located } from "./backend/locate";
import { mcpplsCandidates } from "./backend/mcppls";
import { format, MCPPLS_EXTENSION } from "./mapping";
import type { CommandEntry, InitializeResult } from "./protocol";

export class Session implements vscode.Disposable {
  private backend: Backend | undefined;
  private located: Located | undefined;
  private initResult: InitializeResult | undefined;
  private bundle: Record<string, string> = {};
  private commandTable: CommandEntry[] = [];
  private restarts = 0;
  private starting: Promise<boolean> | undefined;
  private readonly disposables: vscode.Disposable[] = [];
  private readonly listeners = new Map<string, Set<(params: any) => void>>();
  readonly onDidStart = new vscode.EventEmitter<InitializeResult>();

  constructor(readonly context: vscode.ExtensionContext, readonly output: vscode.OutputChannel) {
    // The strings the extension carries (spec/i18n/runtime.zh-cn.json): what is said before the
    // backend runs, or when it cannot.
    if (vscode.env.language.toLowerCase().startsWith("zh")) {
      try {
        this.bundle = JSON.parse(fs.readFileSync(path.join(context.extensionPath, "i18n", "runtime.zh-cn.json"), "utf8"));
      } catch {
        this.bundle = {};
      }
    }
    this.disposables.push(
      this.onDidStart,
      vscode.workspace.onDidChangeConfiguration((e) => {
        if (e.affectsConfiguration("mcpp.ide.backendPath")) { void this.restart(); return; }
        if (e.affectsConfiguration("mcpp")) void this.pushSettings();
      }),
      vscode.workspace.onDidGrantWorkspaceTrust(() => void this.pushSettings()),
      vscode.workspace.onDidChangeWorkspaceFolders(() => void this.restart()),
      vscode.extensions.onDidChange(() => void this.request("languageService/status", { candidates: this.mcpplsCandidates() }).catch(() => undefined)),
    );
  }

  get info(): InitializeResult | undefined {
    return this.initResult;
  }

  get commands(): CommandEntry[] {
    return this.commandTable;
  }

  /** A runtime string in the session's language. */
  t(key: string, ...args: unknown[]): string {
    return format(this.bundle[key] ?? key, args);
  }

  /** The bundle for webviews, which translate on their own. */
  get strings(): Record<string, string> {
    return this.bundle;
  }

  /** Every mcpp.* value, as VS Code resolves it for the window. */
  settings(): Record<string, unknown> {
    const out: Record<string, unknown> = {};
    const props = (this.context.extension.packageJSON?.contributes?.configuration?.properties ?? {}) as Record<string, unknown>;
    const config = vscode.workspace.getConfiguration();
    for (const key of Object.keys(props)) out[key] = config.get(key);
    return out;
  }

  mcpplsCandidates(): string[] {
    return mcpplsCandidates(vscode.extensions.getExtension(MCPPLS_EXTENSION)?.extensionPath);
  }

  on(method: string, fn: (params: any) => void): vscode.Disposable {
    if (!this.listeners.has(method)) this.listeners.set(method, new Set());
    this.listeners.get(method)!.add(fn);
    return new vscode.Disposable(() => this.listeners.get(method)?.delete(fn));
  }

  async start(): Promise<boolean> {
    if (this.starting) return this.starting;
    this.starting = this.doStart().finally(() => { this.starting = undefined; });
    return this.starting;
  }

  private async doStart(): Promise<boolean> {
    const configured = vscode.workspace.getConfiguration().get<string>("mcpp.ide.backendPath") ?? "";
    this.located = locateBackend({ configured: vscode.workspace.isTrusted ? configured : "", extensionRoot: this.context.extensionPath });
    if (!this.located) {
      const message = this.t("The mcpp-ide backend was not found. Install it with `xlings install mcpp-ide`, or set mcpp.ide.backendPath.");
      this.output.appendLine(`mcpp: ${message}`);
      const open = this.t("Open Settings");
      void vscode.window.showErrorMessage(`mcpp: ${message}`, open).then((c) => {
        if (c === open) void vscode.commands.executeCommand("workbench.action.openSettings", "mcpp.ide.backendPath");
      });
      return false;
    }
    this.output.appendLine(`mcpp-ide: starting ${this.located.program} (${this.located.source})`);
    const backend = new Backend({
      program: this.located.program,
      log: (line) => this.output.appendLine(`[backend] ${line}`),
    });
    this.backend = backend;
    backend.notifications.on("*", (method: string, params: unknown) => {
      for (const fn of this.listeners.get(method) ?? []) {
        try { fn(params); } catch (e) { this.output.appendLine(`mcpp: ${method}: ${String(e)}`); }
      }
      for (const fn of this.listeners.get("*") ?? []) fn({ method, params });
    });
    void backend.exited.then((code) => this.onExit(backend, code));
    try {
      const folders = (vscode.workspace.workspaceFolders ?? []).map((f) => f.uri.fsPath);
      this.initResult = await backend.request<InitializeResult>("initialize", {
        client: { name: "VS Code", version: vscode.version },
        editor: "vscode",
        locale: vscode.env.language,
        trusted: vscode.workspace.isTrusted,
        settings: this.settings(),
        workspaceFolders: folders,
        languageService: { candidates: this.mcpplsCandidates() },
      });
      this.bundle = await backend.request<Record<string, string>>("i18n/bundle", {});
      this.commandTable = (await backend.request<{ commands: CommandEntry[] }>("commands/list", { editor: "vscode" })).commands;
      for (const c of this.initResult.checks) if (c.status === "error") this.output.appendLine(`mcpp: ${c.message}`);
      this.onDidStart.fire(this.initResult);
      return true;
    } catch (e) {
      this.output.appendLine(`mcpp-ide: initialize failed: ${String(e)}`);
      return false;
    }
  }

  private onExit(backend: Backend, code: number | null): void {
    if (backend !== this.backend) return;
    this.backend = undefined;
    this.output.appendLine(`mcpp-ide: the backend ended (${code ?? "signal"})`);
    if (this.restarts < 1) {
      ++this.restarts;
      void this.start();
      return;
    }
    const again = this.t("Restart");
    void vscode.window.showErrorMessage(`mcpp: ${this.t("The mcpp-ide backend ended.")}`, again).then((c) => {
      if (c === again) { this.restarts = 0; void this.start(); }
    });
  }

  async restart(): Promise<void> {
    const old = this.backend;
    this.backend = undefined;
    if (old) await old.stop();
    this.restarts = 0;
    await this.start();
  }

  async request<T = unknown>(method: string, params: unknown = {}): Promise<T> {
    if (!this.backend?.running) {
      if (!(await this.start())) throw new BackendError(method, { code: -32099, message: this.t("the mcpp-ide backend is not running") });
    }
    return this.backend!.request<T>(method, params);
  }

  async pushSettings(): Promise<void> {
    if (!this.backend?.running) return;
    await this.backend.request("settings/update", { settings: this.settings(), trusted: vscode.workspace.isTrusted, locale: vscode.env.language });
  }

  dispose(): void {
    this.disposables.forEach((d) => d.dispose());
    void this.backend?.stop();
  }
}
