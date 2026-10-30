/**
 * The session with the mcpp-ide backend (`mcpp-ide serve`): started at activation, given the
 * editor's settings, trust, language and folders, kept current as they change, and restarted (once,
 * then on request) when it ends. The backend is an LSP server (design 2026-10-30 part 2 §8): VS
 * Code's language client starts it and connects to it, which brings mcpp.toml's and build.mcpp's
 * completion, hover, jumps and diagnostics; the views' data adapters and the debugging, testing and
 * packing flows reach mcpp-ide's own methods through `session.request`; the strings are this
 * extension's own (`t`).
 */
import * as vscode from "vscode";
import type * as lc from "vscode-languageclient/node";

import { t } from "../i18n/t";
import { languageServerServes } from "../mcppls/stateSource";
import { backendUnavailableHere, locateBackend, okrunDir, type Located } from "./backend/locate";
import { mcpplsCandidates } from "./backend/mcppls";
import { MCPPLS_EXTENSION } from "./mapping";
import type { CommandEntry, InitializeResult, LanguageServiceStatus } from "./protocol";

// The language client, bundled into dist/vendor (tools/bundle-languageclient.mjs): the VSIX carries
// no node_modules.
// eslint-disable-next-line @typescript-eslint/no-require-imports
const lsp = require("../../vendor/languageclient") as typeof lc;

/** mcpp-ide's own methods and notifications are under `mcpp-ide/` beside LSP's. */
const NAMESPACE = "mcpp-ide/";

export interface RpcError {
  code: number;
  message: string;
  data?: unknown;
}

export class BackendError extends Error {
  constructor(public readonly method: string, public readonly error: RpcError) {
    super(error.message);
  }
}

/** The documents the backend's rules layer reads: mcpp.toml, and build.mcpp (a `cpp` document). */
export const DOCUMENT_SELECTOR = [
  { scheme: "file", language: "mcpp-toml" },
  { scheme: "file", language: "cpp", pattern: "**/build.mcpp" },
];

export class Session implements vscode.Disposable {
  private client: lc.LanguageClient | undefined;
  /** The notifications the current client delivers to `listeners`. */
  private subscribed = new Set<string>();
  private located: Located | undefined;
  private initResult: InitializeResult | undefined;
  private commandTable: CommandEntry[] = [];
  private languageServiceNow: LanguageServiceStatus | undefined;
  private languageServiceAsking: Promise<LanguageServiceStatus | undefined> | undefined;
  /** When the backend ended, within the last three minutes (§3.1). */
  private exits: number[] = [];
  private degradedNow = false;
  private starting: Promise<boolean> | undefined;
  private readonly disposables: vscode.Disposable[] = [];
  private readonly listeners = new Map<string, Set<(params: any) => void>>();
  readonly onDidStart = new vscode.EventEmitter<InitializeResult>();
  /** The backend ended four times in three minutes and is no longer restarted (§3.1, §8). */
  readonly onDidDegrade = new vscode.EventEmitter<void>();

  constructor(readonly context: vscode.ExtensionContext, readonly output: vscode.OutputChannel) {
    this.disposables.push(
      this.onDidStart,
      this.onDidDegrade,
      // The editor regained focus: a `xlings use` or a changed pin happened outside it (§4.2), or a
      // server started or stopped (§5.2).
      vscode.window.onDidChangeWindowState((state) => {
        if (!state.focused || !this.running) return;
        void this.request("project/refresh", {}).then(() => this.onDidRefresh.fire(), () => undefined);
        void this.refreshLanguageService();
      }),
      vscode.workspace.onDidChangeConfiguration((e) => {
        if (e.affectsConfiguration("mcpp.ide.backendPath")) { void this.restart(); return; }
        if (e.affectsConfiguration("mcpp") || e.affectsConfiguration("mcppls.enable")) void this.pushSettings();
      }),
      vscode.workspace.onDidGrantWorkspaceTrust(() => void this.pushSettings()),
      vscode.workspace.onDidChangeWorkspaceFolders(() => void this.restart()),
      // Whether C++ Modules serves build.mcpp: installed, enabled, or switched off for the workspace.
      vscode.extensions.onDidChange(() => { void this.refreshLanguageService(); void this.pushSettings(); }),
      this.onDidChangeLanguageService,
    );
    // mcppls answered differently, or a job ended (a build changes what the server serves).
    this.on("languageService/changed", () => void this.refreshLanguageService());
    this.on("job/finished", () => void this.refreshLanguageService());
  }

  /** What mcppls says about the workspace (§5.2), as last asked; `undefined` before the first answer. */
  get languageService(): LanguageServiceStatus | undefined {
    return this.languageServiceNow;
  }

  /** Fired when `languageService` was asked again. */
  readonly onDidChangeLanguageService = new vscode.EventEmitter<LanguageServiceStatus>();

  /**
   * Asks the backend which mcppls answers and what it says about the workspace: one question at a
   * time, and none while the backend is not running (nothing is started to answer it).
   */
  refreshLanguageService(root?: string): Promise<LanguageServiceStatus | undefined> {
    if (!this.running) return Promise.resolve(this.languageServiceNow);
    if (this.languageServiceAsking) return this.languageServiceAsking;
    const params: Record<string, unknown> = { candidates: this.mcpplsCandidates() };
    const folder = root ?? vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    if (folder !== undefined) params.root = folder;
    this.languageServiceAsking = this
      .request<LanguageServiceStatus>("languageService/status", params)
      .then((status) => {
        this.languageServiceNow = status;
        this.onDidChangeLanguageService.fire(status);
        return status;
      }, () => this.languageServiceNow)
      .finally(() => { this.languageServiceAsking = undefined; });
    return this.languageServiceAsking;
  }

  /** Fired after the projects' mcpp were forgotten, so a view asks again. */
  readonly onDidRefresh = new vscode.EventEmitter<void>();

  /** Whether the backend was given up on after ending too often (§3.1). */
  get degraded(): boolean {
    return this.degradedNow;
  }

  /** The backend's command and where it was found. */
  get started(): Located | undefined {
    return this.located;
  }

  get info(): InitializeResult | undefined {
    return this.initResult;
  }

  get commands(): CommandEntry[] {
    return this.commandTable;
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
    if (this.client) this.subscribe(this.client, method);
    return new vscode.Disposable(() => this.listeners.get(method)?.delete(fn));
  }

  private subscribe(client: lc.LanguageClient, method: string): void {
    if (this.subscribed.has(method)) return;
    this.subscribed.add(method);
    client.onNotification(NAMESPACE + method, (params: unknown) => {
      for (const fn of this.listeners.get(method) ?? []) {
        try { fn(params); } catch (e) { this.output.appendLine(`mcpp: ${method}: ${String(e)}`); }
      }
    });
  }

  /** What the backend is told at the start and on each change. */
  private sessionOptions(): Record<string, unknown> {
    return {
      locale: vscode.env.language,
      trusted: vscode.workspace.isTrusted,
      settings: this.settings(),
      mcpplsServes: languageServerServes(),
    };
  }

  async start(): Promise<boolean> {
    if (this.starting) return this.starting;
    this.starting = this.doStart().finally(() => { this.starting = undefined; });
    return this.starting;
  }

  private async doStart(): Promise<boolean> {
    const configured = vscode.workspace.getConfiguration().get<string>("mcpp.ide.backendPath") ?? "";
    this.located = locateBackend({ configured: vscode.workspace.isTrusted ? configured : "", extensionRoot: this.context.extensionPath });
    if (!this.located && backendUnavailableHere({ extensionRoot: this.context.extensionPath })) {
      // The declared degraded mode: build, run, test and the project pages, without the backend.
      this.degradedNow = true;
      this.onDidDegrade.fire();
      const message = t("The mcpp-ide backend does not run on this platform ({0}); build, run, test and the project pages go on without it.", okrunDir());
      this.output.appendLine(`mcpp: ${message}`);
      void vscode.window.showWarningMessage(`mcpp: ${message}`);
      return false;
    }
    if (!this.located) {
      const message = t("The mcpp-ide backend was not found. Install it with `xlings install mcpp-ide`, or set mcpp.ide.backendPath.");
      this.output.appendLine(`mcpp: ${message}`);
      const open = t("Open Settings");
      void vscode.window.showErrorMessage(`mcpp: ${message}`, open).then((c) => {
        if (c === open) void vscode.commands.executeCommand("workbench.action.openSettings", "mcpp.ide.backendPath");
      });
      return false;
    }
    this.output.appendLine(`mcpp-ide: starting ${[this.located.program, ...this.located.args].join(" ")} (${this.located.source})`);
    const client: lc.LanguageClient = new lsp.LanguageClient("mcpp-ide", "mcpp-ide", {
      command: this.located.program,
      args: [...this.located.args, "serve"],
      transport: lsp.TransportKind.stdio,
    }, {
      documentSelector: DOCUMENT_SELECTOR,
      initializationOptions: {
        editor: "vscode",
        ...this.sessionOptions(),
        languageService: { candidates: this.mcpplsCandidates() },
      },
      // The backend's log (its standard error) and the client's own lines share the mcpp channel.
      outputChannel: this.output,
      revealOutputChannelOn: lsp.RevealOutputChannelOn.Never,
      diagnosticCollectionName: "mcpp",
      initializationFailedHandler: () => false,
      // The restarts are §3.1's, not the client's: 1, 4 and 16 seconds, then the degraded mode.
      errorHandler: {
        error: () => ({ action: lsp.ErrorAction.Continue }),
        closed: () => {
          this.onExit(client);
          return { action: lsp.CloseAction.DoNotRestart, handled: true };
        },
      },
    });
    this.client = client;
    this.subscribed = new Set();
    for (const method of this.listeners.keys()) this.subscribe(client, method);
    try {
      await client.start();
      this.initResult = client.initializeResult as unknown as InitializeResult;
      this.commandTable = (await this.request<{ commands: CommandEntry[] }>("commands/list", { editor: "vscode" })).commands;
      if (this.initResult.languageService) {
        this.languageServiceNow = this.initResult.languageService;
        this.onDidChangeLanguageService.fire(this.initResult.languageService);
      }
      for (const c of this.initResult.checks) if (c.status === "error") this.output.appendLine(`mcpp: ${c.message}`);
      this.onDidStart.fire(this.initResult);
      return true;
    } catch (e) {
      this.output.appendLine(`mcpp-ide: initialize failed: ${String(e)}`);
      return false;
    }
  }

  /**
   * THE BACKEND ENDED (design 2026-10-30 part 2 §3.1). It is started again after 1, 4 and 16
   * seconds; a fourth end within three minutes stops the restarts, and the extension goes on in its
   * declared degraded mode (build, run, test and the project pages) with two actions: start the
   * backend again, and export the IDE report.
   */
  private onExit(client: lc.LanguageClient): void {
    if (client !== this.client) return;
    this.client = undefined;
    this.output.appendLine("mcpp-ide: the backend ended");
    const now = Date.now();
    this.exits = [...this.exits.filter((at) => now - at < 3 * 60_000), now];
    const delays = [1000, 4000, 16000];
    if (this.exits.length <= delays.length) {
      const delay = delays[this.exits.length - 1];
      this.output.appendLine(`mcpp-ide: starting it again in ${delay / 1000} s`);
      setTimeout(() => void this.start(), delay);
      return;
    }
    this.degradedNow = true;
    this.onDidDegrade.fire();
    const again = t("Restart the backend");
    const report = t("Export IDE Report");
    void vscode.window.showErrorMessage(
      `mcpp: ${t("The mcpp-ide backend ended four times in three minutes; build, run, test and the project pages go on without it.")}`,
      again, report,
    ).then((c) => {
      if (c === again) void this.restart();
      if (c === report) void vscode.commands.executeCommand("mcpp.exportReport");
    });
  }

  async restart(): Promise<void> {
    const old = this.client;
    this.client = undefined;
    if (old) await stopClient(old);
    this.exits = [];
    this.degradedNow = false;
    await this.start();
  }

  async request<T = unknown>(method: string, params: unknown = {}): Promise<T> {
    if (this.degradedNow && !this.running) {
      throw new BackendError(method, { code: -32099, message: t("the mcpp-ide backend is not running") });
    }
    if (!this.running && !this.starting) {
      if (!(await this.start())) throw new BackendError(method, { code: -32099, message: t("the mcpp-ide backend is not running") });
    }
    const client = this.client;
    if (!client) throw new BackendError(method, { code: -32099, message: t("the mcpp-ide backend is not running") });
    try {
      return await client.sendRequest<T>(NAMESPACE + method, params);
    } catch (e) {
      if (e instanceof lsp.ResponseError) throw new BackendError(method, { code: e.code, message: e.message, data: e.data });
      throw e;
    }
  }

  async pushSettings(): Promise<void> {
    if (!this.running) return;
    await this.request("settings/update", this.sessionOptions());
  }

  /** Whether a backend is running now (nothing is started to answer this). */
  get running(): boolean {
    return this.client?.isRunning() === true;
  }

  dispose(): void {
    this.onDidRefresh.dispose();
    this.disposables.forEach((d) => d.dispose());
    if (this.client) void stopClient(this.client);
  }
}

/** Shutdown and exit, or the process ended, within a few seconds either way. */
async function stopClient(client: lc.LanguageClient): Promise<void> {
  try {
    await client.stop(3000);
  } catch {
    // it ended already
  }
}
