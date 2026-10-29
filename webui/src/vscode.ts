/**
 * The part of VS Code's extension API that mcpp-vscode's pages use, for the workbench in an IDE that
 * is not VS Code (mcpp-clion's JCEF browser). The pages are mcpp-vscode's own modules, unchanged;
 * what they ask of VS Code is answered here: views and panels by the workbench (workbench.ts),
 * dialogs, files, settings, the clipboard and the IDE's own commands by the IDE (bridge.ts).
 */
import zhCn from "../../mide-plugins/vscode/l10n/bundle.l10n.zh-cn.json";
import { info, on, request } from "./bridge";
import { workbench } from "./workbench";

// ── events and disposables ──────────────────────────────────────────────────────────────────────

export class Disposable {
  constructor(private readonly fn: () => void) {}
  static from(...items: Array<{ dispose(): unknown }>): Disposable {
    return new Disposable(() => items.forEach((i) => i.dispose()));
  }
  dispose(): void {
    this.fn();
  }
}

export type Event<T> = (listener: (e: T) => unknown) => Disposable;

export class EventEmitter<T> {
  private readonly listeners = new Set<(e: T) => unknown>();
  readonly event: Event<T> = (listener) => {
    this.listeners.add(listener);
    return new Disposable(() => this.listeners.delete(listener));
  };
  fire(value: T): void {
    for (const l of [...this.listeners]) {
      try { l(value); } catch (e) { console.error(e); }
    }
  }
  dispose(): void {
    this.listeners.clear();
  }
}

// ── values ──────────────────────────────────────────────────────────────────────────────────────

export class Uri {
  private constructor(readonly scheme: string, readonly authority: string, readonly path: string, readonly query: string, readonly fragment: string) {}

  static parse(text: string): Uri {
    const m = /^([a-zA-Z][\w+.-]*):(?:\/\/([^/?#]*))?([^?#]*)(?:\?([^#]*))?(?:#(.*))?$/.exec(text);
    if (!m) return new Uri("file", "", text, "", "");
    return new Uri(m[1], m[2] ?? "", decodeURIComponent(m[3] ?? ""), m[4] ?? "", m[5] ?? "");
  }

  static file(fsPath: string): Uri {
    let p = fsPath.replace(/\\/g, "/");
    if (/^[A-Za-z]:/.test(p)) p = `/${p}`;
    return new Uri("file", "", p, "", "");
  }

  static joinPath(base: Uri, ...segments: string[]): Uri {
    const joined = [base.path.replace(/\/+$/, ""), ...segments.map((s) => s.replace(/^\/+|\/+$/g, ""))].join("/");
    return base.with({ path: joined });
  }

  get fsPath(): string {
    const p = /^\/[A-Za-z]:/.test(this.path) ? this.path.slice(1) : this.path;
    return info.platform === "win32" ? p.replace(/\//g, "\\") : p;
  }

  with(change: { scheme?: string; path?: string; query?: string; fragment?: string }): Uri {
    return new Uri(change.scheme ?? this.scheme, this.authority, change.path ?? this.path, change.query ?? this.query, change.fragment ?? this.fragment);
  }

  toString(): string {
    const authority = this.scheme === "file" || this.authority ? `//${this.authority}` : "";
    const path = this.path.split("/").map(encodeURIComponent).join("/").replace(/%3A/g, ":");
    return `${this.scheme}:${authority}${path}${this.query ? `?${this.query}` : ""}${this.fragment ? `#${this.fragment}` : ""}`;
  }

  toJSON(): string {
    return this.toString();
  }
}

export class ThemeColor {
  constructor(readonly id: string) {}
}

export class ThemeIcon {
  constructor(readonly id: string, readonly color?: ThemeColor) {}
}

export enum TreeItemCollapsibleState { None = 0, Collapsed = 1, Expanded = 2 }

export class TreeItem {
  id?: string;
  description?: string;
  tooltip?: string;
  iconPath?: ThemeIcon;
  contextValue?: string;
  command?: { command: string; title: string; arguments?: unknown[] };
  accessibilityInformation?: { label: string };
  constructor(public label: string, public collapsibleState: TreeItemCollapsibleState = TreeItemCollapsibleState.None) {}
}

export class RelativePattern {
  readonly baseUri: Uri;
  readonly base: string;
  constructor(base: Uri | string | { uri: Uri }, readonly pattern: string) {
    this.baseUri = typeof base === "string" ? Uri.file(base) : base instanceof Uri ? base : base.uri;
    this.base = this.baseUri.fsPath;
  }
}

export enum ViewColumn { Active = -1, Beside = -2, One = 1 }
export enum ProgressLocation { SourceControl = 1, Window = 10, Notification = 15 }
export enum ConfigurationTarget { Global = 1, Workspace = 2, WorkspaceFolder = 3 }
export enum StatusBarAlignment { Left = 1, Right = 2 }

// ── settings ────────────────────────────────────────────────────────────────────────────────────

/** Every mcpp.* value, by level: the IDE's own store, read once and kept current. */
interface SettingLevels { user: Record<string, unknown>; workspace: Record<string, unknown> }
let levels: SettingLevels = { user: {}, workspace: {} };
const configurationChanged = new EventEmitter<{ affectsConfiguration(section: string): boolean }>();

export async function loadSettings(): Promise<void> {
  levels = await request<SettingLevels>("settings.read");
}

on("settingsChanged", (changed: { keys: string[]; levels: SettingLevels }) => {
  levels = changed.levels;
  configurationChanged.fire({ affectsConfiguration: (section) => changed.keys.some((k) => k === section || k.startsWith(`${section}.`)) });
});

function configuration(section?: string) {
  const full = (key: string) => (section ? `${section}.${key}` : key);
  return {
    get<T>(key: string, fallback?: T): T | undefined {
      const k = full(key);
      return (levels.workspace[k] ?? levels.user[k] ?? fallback) as T | undefined;
    },
    has(key: string): boolean {
      const k = full(key);
      return k in levels.workspace || k in levels.user;
    },
    inspect<T>(key: string) {
      const k = full(key);
      return { key: k, globalValue: levels.user[k] as T | undefined, workspaceValue: levels.workspace[k] as T | undefined, workspaceFolderValue: undefined };
    },
    async update(key: string, value: unknown, target?: ConfigurationTarget | boolean): Promise<void> {
      const k = full(key);
      const level = target === ConfigurationTarget.Global || target === true ? "user" : "workspace";
      levels = await request<SettingLevels>("settings.write", { key: k, value: value ?? null, level });
      configurationChanged.fire({ affectsConfiguration: (s) => k === s || k.startsWith(`${s}.`) });
    },
  };
}

// ── documents ───────────────────────────────────────────────────────────────────────────────────

const contentProviders = new Map<string, { provideTextDocumentContent(uri: Uri): string | Promise<string> }>();
const saved = new EventEmitter<{ uri: Uri; fileName: string; languageId: string }>();
on("documentSaved", (p: { path: string }) => saved.fire({ uri: Uri.file(p.path), fileName: p.path, languageId: "" }));

class TextDocument {
  constructor(readonly uri: Uri, private readonly text: string) {}
  get fileName(): string { return this.uri.fsPath; }
  getText(): string { return this.text; }
}

async function openTextDocument(target: Uri | string): Promise<TextDocument> {
  const uri = typeof target === "string" ? Uri.file(target) : target;
  const provider = contentProviders.get(uri.scheme);
  if (provider) return new TextDocument(uri, await provider.provideTextDocumentContent(uri));
  return new TextDocument(uri, (await request<string | null>("fs.read", { path: uri.fsPath })) ?? "");
}

async function showTextDocument(target: Uri | TextDocument, options?: { selection?: { start: { line: number } } }): Promise<unknown> {
  const doc = target instanceof TextDocument ? target : undefined;
  const uri = doc ? doc.uri : (target as Uri);
  const line = options?.selection?.start?.line;
  if (uri.scheme === "file") return request("openFile", { path: uri.fsPath, line: line === undefined ? undefined : line + 1 });
  const text = doc ? doc.getText() : (await openTextDocument(uri)).getText();
  return request("openText", { name: uri.path.split("/").pop() ?? uri.path, text });
}

// ── file watchers ───────────────────────────────────────────────────────────────────────────────

/** A glob over `/`-separated names: `**`, `*`, `?` and `{a,b}`. */
/** A glob as VS Code reads one: `**`, `*`, `?`, and `{a,b}` whose alternatives are globs again. */
function globSource(glob: string): string {
  let re = "";
  for (let i = 0; i < glob.length; i++) {
    const c = glob[i];
    if (c === "*" && glob[i + 1] === "*") {
      i++;
      if (glob[i + 1] === "/") { re += "(?:.*/)?"; i++; } else re += ".*";
    }
    else if (c === "*") re += "[^/]*";
    else if (c === "?") re += "[^/]";
    else if (c === "{") { const end = glob.indexOf("}", i); re += `(?:${glob.slice(i + 1, end).split(",").map(globSource).join("|")})`; i = end; }
    else re += c.replace(/[.+^$()|[\]\\]/g, "\\$&");
  }
  return re;
}

function globToRegExp(glob: string): RegExp {
  return new RegExp(`^${globSource(glob)}$`);
}

const watchers = new Set<{ match(path: string): boolean; created: EventEmitter<Uri>; changed: EventEmitter<Uri>; deleted: EventEmitter<Uri> }>();
on("fileChanged", (p: { path: string; kind: "created" | "changed" | "deleted" }) => {
  for (const w of watchers) {
    if (!w.match(p.path)) continue;
    (p.kind === "created" ? w.created : p.kind === "deleted" ? w.deleted : w.changed).fire(Uri.file(p.path));
  }
});

function createFileSystemWatcher(pattern: string | RelativePattern) {
  const slash = (s: string) => s.replace(/\\/g, "/");
  const base = typeof pattern === "string" ? "" : slash(pattern.base).replace(/\/$/, "");
  const re = globToRegExp(typeof pattern === "string" ? pattern : pattern.pattern);
  const w = {
    match: (path: string) => {
      const p = slash(path);
      if (base && !p.startsWith(`${base}/`)) return false;
      const rel = base ? p.slice(base.length + 1) : p;
      return re.test(rel) || (!base && re.test(p.split("/").slice(-rel.split("/").length).join("/"))) || re.test(p.replace(/^.*\//, ""));
    },
    created: new EventEmitter<Uri>(), changed: new EventEmitter<Uri>(), deleted: new EventEmitter<Uri>(),
  };
  watchers.add(w);
  request("watch", { base: base || undefined, pattern: typeof pattern === "string" ? pattern : pattern.pattern }).catch(() => undefined);
  return {
    onDidCreate: w.created.event, onDidChange: w.changed.event, onDidDelete: w.deleted.event,
    dispose: () => watchers.delete(w),
  };
}

// ── commands ────────────────────────────────────────────────────────────────────────────────────

const registry = new Map<string, (...args: unknown[]) => unknown>();

export const commands = {
  registerCommand(id: string, fn: (...args: unknown[]) => unknown): Disposable {
    registry.set(id, fn);
    return new Disposable(() => registry.delete(id));
  },
  async executeCommand<T = unknown>(id: string, ...args: unknown[]): Promise<T | undefined> {
    const own = registry.get(id);
    if (own) return (await own(...args)) as T;
    if (id === "setContext") { workbench.setContext(String(args[0]), args[1]); return undefined; }
    if (id.endsWith(".focus")) { workbench.focus(id.slice(0, -".focus".length)); return undefined; }
    if (id === "workbench.action.openSettings") { return (await commands.executeCommand("mcpp.openSettings")) as T; }
    // Everything else is the IDE's: mcpp's commands (spec/commands.json), and the editor's own.
    return request<T>("command", { id, args });
  },
  async getCommands(): Promise<string[]> {
    return [...registry.keys()];
  },
};

// ── window ──────────────────────────────────────────────────────────────────────────────────────

type MessageItem = string | { title: string; isCloseAffordance?: boolean };

async function showMessage(level: "info" | "warning" | "error", message: string, ...rest: unknown[]): Promise<MessageItem | undefined> {
  const options = rest.length > 0 && typeof rest[0] === "object" && rest[0] !== null && !("title" in (rest[0] as object)) ? (rest.shift() as { modal?: boolean; detail?: string }) : {};
  const items = rest.filter((i) => i !== undefined) as MessageItem[];
  const titles = items.map((i) => (typeof i === "string" ? i : i.title));
  const chosen = await request<number | null>("message", { level, message, detail: options.detail, modal: options.modal === true, items: titles });
  return chosen === null || chosen < 0 ? undefined : items[chosen];
}

const activeEditorChanged = new EventEmitter<unknown>();
let activeEditorPath: string | undefined;
on("activeEditor", (p: { path?: string }) => {
  activeEditorPath = p.path;
  activeEditorChanged.fire(window.activeTextEditor);
});

export const window = {
  showInformationMessage: (message: string, ...rest: unknown[]) => showMessage("info", message, ...rest),
  showWarningMessage: (message: string, ...rest: unknown[]) => showMessage("warning", message, ...rest),
  showErrorMessage: (message: string, ...rest: unknown[]) => showMessage("error", message, ...rest),
  async showInputBox(options: { title?: string; prompt?: string; value?: string; placeHolder?: string; validateInput?: (v: string) => unknown } = {}): Promise<string | undefined> {
    for (;;) {
      const value = await request<string | null>("input", { title: options.title, prompt: options.prompt, value: options.value, placeHolder: options.placeHolder });
      if (value === null) return undefined;
      const problem = options.validateInput ? await options.validateInput(value) : undefined;
      if (!problem) return value;
      await showMessage("warning", typeof problem === "string" ? problem : String((problem as { message?: string }).message ?? problem));
      options = { ...options, value };
    }
  },
  async showQuickPick<T extends string | { label: string; description?: string; detail?: string }>(items: readonly T[] | Promise<readonly T[]>, options: { title?: string; placeHolder?: string } = {}): Promise<T | undefined> {
    const list = await items;
    const rows = list.map((i) => (typeof i === "string" ? { label: i } : { label: i.label, description: i.description, detail: i.detail }));
    const chosen = await request<number | null>("pick", { title: options.title, placeHolder: options.placeHolder, items: rows });
    return chosen === null || chosen < 0 ? undefined : list[chosen];
  },
  async showSaveDialog(options: { defaultUri?: Uri; filters?: Record<string, string[]>; title?: string } = {}): Promise<Uri | undefined> {
    const path = await request<string | null>("saveDialog", { path: options.defaultUri?.fsPath, filters: options.filters, title: options.title });
    return path === null ? undefined : Uri.file(path);
  },
  async withProgress<R>(options: { location: ProgressLocation; title?: string; cancellable?: boolean }, task: (progress: { report(v: { message?: string; increment?: number }): void }, token: { isCancellationRequested: boolean; onCancellationRequested: Event<unknown> }) => Thenable<R> | Promise<R>): Promise<R> {
    const id = await request<number>("progress.start", { title: options.title ?? "" });
    const cancelled = new EventEmitter<unknown>();
    try {
      return await task(
        { report: (v) => { if (v.message) request("progress.report", { id, message: v.message }).catch(() => undefined); } },
        { isCancellationRequested: false, onCancellationRequested: cancelled.event },
      );
    } finally {
      request("progress.end", { id }).catch(() => undefined);
    }
  },
  showTextDocument,
  get activeTextEditor(): { document: { uri: Uri; fileName: string } } | undefined {
    return activeEditorPath ? { document: { uri: Uri.file(activeEditorPath), fileName: activeEditorPath } } : undefined;
  },
  onDidChangeActiveTextEditor: activeEditorChanged.event,
  createStatusBarItem() {
    // The IDE's status bar is its own; the workbench shows none.
    return { text: "", tooltip: "", command: undefined as unknown, show() {}, hide() {}, dispose() {} };
  },
  createOutputChannel(name: string) {
    return {
      name,
      append: (text: string) => { request("log", { text }).catch(() => undefined); },
      appendLine: (text: string) => { request("log", { text: `${text}\n` }).catch(() => undefined); },
      show: () => { request("log.show").catch(() => undefined); },
      clear() {}, hide() {}, dispose() {}, replace() {},
    };
  },
  registerWebviewViewProvider: (id: string, provider: { resolveWebviewView(view: unknown, context: unknown, token: unknown): unknown }) =>
    workbench.registerView(id, provider),
  createWebviewPanel: (viewType: string, title: string, _column: unknown, options: unknown) => workbench.createPanel(viewType, title, options),
  createTreeView: (id: string, options: { treeDataProvider: unknown }) =>
    workbench.registerTree(id, options.treeDataProvider, (command, args) => commands.executeCommand(command, ...args)),
  registerTreeDataProvider: (id: string, provider: unknown) =>
    workbench.registerTree(id, provider, (command, args) => commands.executeCommand(command, ...args)),
};

// ── workspace ───────────────────────────────────────────────────────────────────────────────────

const trustGranted = new EventEmitter<void>();
const foldersChanged = new EventEmitter<unknown>();
on("trust", (p: { trusted: boolean }) => { info.trusted = p.trusted; if (p.trusted) trustGranted.fire(); });

function folders() {
  return info.workspaceFolders.map((f, index) => ({ uri: Uri.file(f), name: f.split(/[\\/]/).pop() ?? f, index }));
}

export const workspace = {
  get isTrusted(): boolean { return info.trusted; },
  get workspaceFolders() { return folders(); },
  getWorkspaceFolder(uri: Uri) {
    const p = uri.fsPath.replace(/\\/g, "/");
    return folders().find((f) => p === f.uri.fsPath.replace(/\\/g, "/") || p.startsWith(`${f.uri.fsPath.replace(/\\/g, "/")}/`));
  },
  getConfiguration: (section?: string, _resource?: unknown) => configuration(section),
  onDidChangeConfiguration: configurationChanged.event,
  onDidSaveTextDocument: saved.event,
  onDidGrantWorkspaceTrust: trustGranted.event,
  onDidChangeWorkspaceFolders: foldersChanged.event,
  openTextDocument,
  registerTextDocumentContentProvider(scheme: string, provider: { provideTextDocumentContent(uri: Uri): string | Promise<string> }): Disposable {
    contentProviders.set(scheme, provider);
    return new Disposable(() => contentProviders.delete(scheme));
  },
  createFileSystemWatcher,
  async findFiles(pattern: string | RelativePattern, exclude?: string, max?: number): Promise<Uri[]> {
    const base = typeof pattern === "string" ? info.workspaceFolders[0] : pattern.base;
    const glob = typeof pattern === "string" ? pattern : pattern.pattern;
    const found = await request<string[]>("findFiles", { base, pattern: glob, exclude, max });
    return found.map((p) => Uri.file(p));
  },
  fs: {
    async writeFile(uri: Uri, bytes: Uint8Array): Promise<void> {
      let text = "";
      for (let i = 0; i < bytes.length; i += 0x8000) text += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
      await request("fs.write", { path: uri.fsPath, text: btoa(text), base64: true });
    },
    async readFile(uri: Uri): Promise<Uint8Array> {
      return new TextEncoder().encode((await request<string | null>("fs.read", { path: uri.fsPath })) ?? "");
    },
  },
};

// ── the rest ────────────────────────────────────────────────────────────────────────────────────

export const env = {
  get language(): string { return info.language; },
  clipboard: { writeText: (text: string) => request("clipboard", { text }).then(() => undefined) },
  openExternal: (uri: Uri) => request<boolean>("openExternal", { url: uri.toString() }),
};

export const extensions = {
  // The C++ Modules language service is the IDE's (the mcppls plugin); no VS Code extension is.
  getExtension: (_id: string) => undefined,
  onDidChange: new EventEmitter<void>().event,
};

export const tasks = {
  // A build in the IDE ends in the backend's result record: the pages refresh as on a task's end.
  onDidEndTaskProcess: (listener: (e: unknown) => unknown) => on("result/changed", () => listener({ execution: { task: { definition: { type: "mcpp" } } }, exitCode: 0 })),
};

function format(template: string, args: unknown[]): string {
  return args.reduce<string>((text, a, i) => text.split(`{${i}}`).join(String(a)), template);
}

export const l10n = {
  t(message: string | { message: string; args?: unknown[] }, ...args: unknown[]): string {
    const text = typeof message === "string" ? message : message.message;
    const values = typeof message === "string" ? args : (message.args ?? []);
    const bundle = info.language.toLowerCase().startsWith("zh") ? (zhCn as Record<string, string>) : {};
    return format(bundle[text] ?? text, values);
  },
};

export const version = "1.91.0";
