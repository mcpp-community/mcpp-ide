/**
 * The workbench: what VS Code's own window does for mcpp-vscode's pages, in one browser page.
 *
 * Views (the project tree, the libraries, the cache) and panels (the settings, a package's page,
 * the graphs) are tabs. A webview's document is an iframe whose page is the document the page code
 * assigned, with `acquireVsCodeApi()` and the theme's `--vscode-*` variables put in front of it, so
 * the page runs as it runs in VS Code: messages both ways, state across reloads. A tree view renders
 * the provider's items as VS Code's tree does, with the inline actions the manifest names.
 */
import manifest from "../../mide-plugins/vscode/package.json";
import nlsEn from "../../mide-plugins/vscode/package.nls.json";
import nlsZh from "../../mide-plugins/vscode/package.nls.zh-cn.json";
import { info, request } from "./bridge";

type Listener<T> = (e: T) => unknown;

class Emitter<T> {
  private readonly ls = new Set<Listener<T>>();
  event = (l: Listener<T>) => { this.ls.add(l); return { dispose: () => this.ls.delete(l) }; };
  fire(v: T): void { for (const l of [...this.ls]) { try { l(v); } catch (e) { console.error(e); } } }
}

/** A manifest string (`%key%`) in the IDE's language. */
function nls(text: string | undefined): string {
  if (!text) return "";
  const m = /^%(.+)%$/.exec(text);
  if (!m) return text;
  const zh = info.language.toLowerCase().startsWith("zh");
  return ((zh ? (nlsZh as Record<string, string>)[m[1]] : undefined) ?? (nlsEn as Record<string, string>)[m[1]] ?? text);
}

// ── context keys and `when` ─────────────────────────────────────────────────────────────────────

const context = new Map<string, unknown>();
const contextChanged = new Emitter<void>();

/** The `when` forms the manifest uses: `a`, `!a`, `a == b`, `a != b`, `a =~ /re/`, `&&`, `||`. */
function when(expression: string | undefined, extra: Record<string, unknown> = {}): boolean {
  if (!expression) return true;
  const value = (name: string): unknown => (name in extra ? extra[name] : context.get(name));
  return expression.split("||").some((alternative) => alternative.split("&&").every((raw) => {
    const term = raw.trim();
    let m = /^(\S+)\s*=~\s*\/(.*)\/([a-z]*)$/.exec(term);
    if (m) return new RegExp(m[2], m[3]).test(String(value(m[1]) ?? ""));
    m = /^(\S+)\s*(==|!=)\s*(.+)$/.exec(term);
    if (m) {
      const right = m[3].trim().replace(/^'(.*)'$/, "$1");
      const equal = String(value(m[1])) === right;
      return m[2] === "==" ? equal : !equal;
    }
    if (term.startsWith("!")) return !value(term.slice(1).trim());
    return Boolean(value(term));
  }));
}

// ── the frame of a webview ──────────────────────────────────────────────────────────────────────

let theme: Record<string, string> = {};
let themeKind: "vscode-light" | "vscode-dark" | "vscode-high-contrast" = "vscode-light";
const frames = new Map<string, Webview>();
let nextFrame = 1;

window.addEventListener("message", (e: MessageEvent) => {
  const data = e.data as { __mcppWebview?: string; message?: unknown; state?: unknown; kind?: string };
  if (!data || typeof data.__mcppWebview !== "string") return;
  const webview = frames.get(data.__mcppWebview);
  if (!webview) return;
  if (data.kind === "state") webview.state = data.state;
  else webview.received.fire(data.message);
});

class Webview {
  readonly id = `w${nextFrame++}`;
  readonly received = new Emitter<unknown>();
  readonly onDidReceiveMessage = this.received.event;
  options: unknown = {};
  state: unknown = undefined;
  private document = "";
  constructor(readonly frame: HTMLIFrameElement) {
    frames.set(this.id, this);
  }

  /** Where the page's own files are, as a CSP source: `file:` (a scheme) for the files the IDE extracted. */
  get cspSource(): string {
    const url = new URL(info.extensionUri);
    return url.protocol === "file:" ? "file:" : url.origin;
  }

  asWebviewUri(uri: { toString(): string }): { toString(): string } {
    return uri;
  }

  get html(): string {
    return this.document;
  }

  set html(document: string) {
    this.document = document;
    const nonce = /'nonce-([^']+)'/.exec(document)?.[1] ?? "";
    const vars = Object.entries(theme).map(([k, v]) => `${k}: ${v};`).join(" ");
    const prelude = `<style nonce="${nonce}">:root { ${vars} } body { margin: 0; padding: 0 20px; color: var(--vscode-foreground); background: var(--vscode-editor-background); font-family: var(--vscode-font-family); font-size: var(--vscode-font-size); }</style>`
      + `<script nonce="${nonce}">(function () { var id = ${JSON.stringify(this.id)}; var state = ${JSON.stringify(this.state ?? null)};`
      + ` window.acquireVsCodeApi = function () { return { postMessage: function (m) { parent.postMessage({ __mcppWebview: id, message: m }, "*"); },`
      + ` getState: function () { return state; }, setState: function (s) { state = s; parent.postMessage({ __mcppWebview: id, kind: "state", state: s }, "*"); return s; } }; };`
      + ` document.addEventListener("DOMContentLoaded", function () { document.body.classList.add(${JSON.stringify(themeKind)}); }); })();</script>`;
    const withPrelude = /<head[^>]*>/i.test(document) ? document.replace(/<head[^>]*>/i, (h) => `${h}${prelude}`) : `${prelude}${document}`;
    this.frame.srcdoc = withPrelude;
  }

  async postMessage(message: unknown): Promise<boolean> {
    this.frame.contentWindow?.postMessage(message, "*");
    return true;
  }
}

// ── tabs ────────────────────────────────────────────────────────────────────────────────────────

interface Tab {
  key: string;
  title: string;
  closable: boolean;
  /** A view of the manifest: shown while its `when` holds. */
  when?: string;
  body: HTMLElement;
  button?: HTMLButtonElement;
  shown(): void;
  hidden(): void;
  closed(): void;
}

const tabs: Tab[] = [];
let current: Tab | undefined;
let strip: HTMLElement;
let stage: HTMLElement;

function layout(): void {
  if (strip) return;
  strip = document.createElement("nav");
  strip.className = "tabs";
  stage = document.createElement("main");
  stage.className = "stage";
  document.body.append(strip, stage);
}

function renderStrip(): void {
  layout();
  strip.replaceChildren();
  for (const tab of tabs) {
    if (!when(tab.when)) continue;
    const b = document.createElement("button");
    b.className = `tab${tab === current ? " active" : ""}`;
    b.textContent = tab.title;
    b.onclick = () => select(tab);
    if (tab.closable) {
      const x = document.createElement("span");
      x.className = "codicon codicon-close close";
      x.title = "Close";
      x.onclick = (e) => { e.stopPropagation(); close(tab); };
      b.append(x);
    }
    tab.button = b;
    strip.append(b);
  }
}

function select(tab: Tab): void {
  layout();
  if (current === tab) { renderStrip(); return; }
  current?.hidden();
  if (current) current.body.hidden = true;
  current = tab;
  tab.body.hidden = false;
  tab.shown();
  renderStrip();
}

function add(tab: Tab): void {
  layout();
  tab.body.hidden = true;
  stage.append(tab.body);
  tabs.push(tab);
  renderStrip();
}

function close(tab: Tab): void {
  const i = tabs.indexOf(tab);
  if (i < 0) return;
  tabs.splice(i, 1);
  tab.body.remove();
  tab.closed();
  if (current === tab) {
    current = undefined;
    const next = tabs[Math.max(0, i - 1)];
    if (next) select(next);
  }
  renderStrip();
}

contextChanged.event(() => {
  renderStrip();
  if (current && !when(current.when)) {
    const next = tabs.find((t) => when(t.when));
    if (next) select(next);
  }
});

/** The views the manifest contributes, in its order, with their titles and `when`. */
const declaredViews: Array<{ id: string; name?: string; when?: string; type?: string }> =
  ((manifest as { contributes?: { views?: Record<string, Array<{ id: string; name?: string; when?: string; type?: string }>> } }).contributes?.views?.mcpp) ?? [];

function viewTab(id: string, body: HTMLElement, shown: () => void, hidden: () => void): Tab {
  const declared = declaredViews.find((v) => v.id === id);
  const tab: Tab = { key: id, title: nls(declared?.name) || id, closable: false, when: declared?.when, body, shown, hidden, closed() {} };
  // The manifest's order, whatever order the pages registered in.
  const order = (key: string) => { const i = declaredViews.findIndex((v) => v.id === key); return i < 0 ? 1000 : i; };
  const at = tabs.findIndex((t) => !t.closable && order(t.key) > order(id));
  layout();
  body.hidden = true;
  stage.append(body);
  if (at < 0) tabs.splice(tabs.filter((t) => !t.closable).length, 0, tab); else tabs.splice(at, 0, tab);
  renderStrip();
  return tab;
}

// ── tree views ──────────────────────────────────────────────────────────────────────────────────

interface TreeItemLike {
  label: string; id?: string; description?: string; tooltip?: string; contextValue?: string; collapsibleState?: number;
  iconPath?: { id: string; color?: { id: string } };
  command?: { command: string; arguments?: unknown[] };
}
interface Provider<T> {
  getChildren(element?: T): T[] | Promise<T[]>;
  getTreeItem(element: T): TreeItemLike | Promise<TreeItemLike>;
  onDidChangeTreeData?: (l: () => unknown) => unknown;
}

const inlineActions: Array<{ command: string; when?: string; group?: string }> =
  ((manifest as { contributes?: { menus?: Record<string, Array<{ command: string; when?: string; group?: string }>> } }).contributes?.menus?.["view/item/context"]) ?? [];
const commandIcons = new Map<string, { title: string; icon?: string }>(
  ((manifest as { contributes?: { commands?: Array<{ command: string; title: string; icon?: string }> } }).contributes?.commands ?? [])
    .map((c) => [c.command, { title: nls(c.title), icon: c.icon }]),
);

function treeView<T>(id: string, provider: Provider<T>, run: (command: string, args: unknown[]) => void) {
  const body = document.createElement("section");
  body.className = "tree";
  const expanded = new Map<string, boolean>();
  const changedVisibility = new Emitter<{ visible: boolean }>();
  let visible = false;
  const draw = async (): Promise<void> => {
    const rows: HTMLElement[] = [];
    const walk = async (element: T | undefined, depth: number): Promise<void> => {
      for (const child of await provider.getChildren(element)) {
        const item = await provider.getTreeItem(child);
        const key = item.id ?? `${depth}:${item.label}`;
        const collapsible = (item.collapsibleState ?? 0) !== 0;
        const open = expanded.get(key) ?? item.collapsibleState === 2;
        const row = document.createElement("div");
        row.className = "row";
        row.style.paddingLeft = `${8 + depth * 16}px`;
        row.title = item.tooltip ?? "";
        row.setAttribute("role", "treeitem");
        if (collapsible) row.setAttribute("aria-expanded", String(open));
        const twistie = document.createElement("span");
        twistie.className = `twistie codicon ${collapsible ? (open ? "codicon-chevron-down" : "codicon-chevron-right") : ""}`;
        row.append(twistie);
        if (item.iconPath?.id) {
          const icon = document.createElement("span");
          icon.className = `icon codicon codicon-${item.iconPath.id}`;
          if (item.iconPath.color) icon.style.color = `var(--vscode-${item.iconPath.color.id.replace(/\./g, "-")})`;
          row.append(icon);
        }
        const label = document.createElement("span");
        label.className = "label";
        label.textContent = item.label;
        row.append(label);
        if (item.description) {
          const d = document.createElement("span");
          d.className = "description";
          d.textContent = item.description;
          row.append(d);
        }
        for (const action of inlineActions) {
          if (!(action.group ?? "").startsWith("inline")) continue;
          if (!when(action.when, { view: id, viewItem: item.contextValue ?? "" })) continue;
          const declared = commandIcons.get(action.command);
          const b = document.createElement("span");
          const icon = /^\$\((.+)\)$/.exec(declared?.icon ?? "")?.[1];
          b.className = `action codicon ${icon ? `codicon-${icon}` : "codicon-arrow-right"}`;
          b.title = declared?.title ?? action.command;
          b.onclick = (e) => { e.stopPropagation(); run(action.command, [child]); };
          row.append(b);
        }
        row.onclick = () => {
          if (collapsible) { expanded.set(key, !open); void draw(); return; }
          if (item.command) run(item.command.command, item.command.arguments ?? []);
        };
        rows.push(row);
        if (collapsible && open) await walk(child, depth + 1);
      }
    };
    await walk(undefined, 0);
    body.replaceChildren(...rows);
  };
  provider.onDidChangeTreeData?.(() => { if (visible) void draw(); });
  const tab = viewTab(id, body, () => { visible = true; changedVisibility.fire({ visible: true }); void draw(); }, () => { visible = false; changedVisibility.fire({ visible: false }); });
  return {
    get visible() { return visible; },
    onDidChangeVisibility: changedVisibility.event,
    reveal: async () => select(tab),
    dispose: () => close(tab),
    title: tab.title,
    description: undefined as string | undefined,
    message: undefined as string | undefined,
    badge: undefined as unknown,
  };
}

// ── webview views and panels ────────────────────────────────────────────────────────────────────

function frame(): HTMLIFrameElement {
  const f = document.createElement("iframe");
  f.className = "webview";
  return f;
}

export const workbench = {
  /** The IDE's theme, as the `--vscode-*` variables the pages read, and the theme's kind. */
  setTheme(vars: Record<string, string>, kind: typeof themeKind): void {
    theme = vars;
    themeKind = kind;
    document.documentElement.setAttribute("style", Object.entries(vars).map(([k, v]) => `${k}: ${v}`).join("; "));
    document.body.classList.remove("vscode-light", "vscode-dark", "vscode-high-contrast");
    document.body.classList.add(kind);
  },

  setContext(key: string, value: unknown): void {
    context.set(key, value);
    contextChanged.fire();
  },

  /** Brings a view or a panel on screen: the IDE's request, or a page's `<view>.focus`. */
  focus(key: string): void {
    const tab = tabs.find((t) => t.key === key) ?? tabs.find((t) => t.key.endsWith(`.${key}`));
    if (tab) select(tab);
  },

  /** The first view whose `when` holds, when nothing is on screen yet. */
  showFirst(): void {
    if (current) return;
    const tab = tabs.find((t) => when(t.when));
    if (tab) select(tab);
  },

  registerView(id: string, provider: { resolveWebviewView(view: unknown, context: unknown, token: unknown): unknown }) {
    const body = document.createElement("section");
    body.className = "view";
    const f = frame();
    body.append(f);
    const webview = new Webview(f);
    const changed = new Emitter<void>();
    const disposed = new Emitter<void>();
    let visible = false;
    let resolved = false;
    const view = {
      webview,
      get visible() { return visible; },
      onDidChangeVisibility: changed.event,
      onDidDispose: disposed.event,
      show: () => workbench.focus(id),
      title: undefined as string | undefined,
      description: undefined as string | undefined,
    };
    viewTab(id, body, () => {
      visible = true;
      if (!resolved) { resolved = true; void provider.resolveWebviewView(view, { state: undefined }, { isCancellationRequested: false }); }
      changed.fire();
    }, () => { visible = false; changed.fire(); });
    return { dispose() {} };
  },

  createPanel(viewType: string, title: string, options: unknown) {
    const body = document.createElement("section");
    body.className = "panel";
    const f = frame();
    body.append(f);
    const webview = new Webview(f);
    webview.options = options;
    const disposed = new Emitter<void>();
    const viewState = new Emitter<{ webviewPanel: unknown }>();
    let visible = false;
    let isDisposed = false;
    const panel = {
      viewType,
      webview,
      get title() { return tab.title; },
      set title(t: string) { tab.title = t; renderStrip(); },
      get visible() { return visible; },
      get active() { return visible; },
      iconPath: undefined as unknown,
      onDidDispose: disposed.event,
      onDidChangeViewState: viewState.event,
      reveal: () => select(tab),
      dispose: () => { if (!isDisposed) close(tab); },
    };
    const tab: Tab = {
      key: `${viewType}#${webview.id}`, title, closable: true, body,
      shown: () => { visible = true; viewState.fire({ webviewPanel: panel }); },
      hidden: () => { visible = false; viewState.fire({ webviewPanel: panel }); },
      closed: () => { isDisposed = true; visible = false; frames.delete(webview.id); disposed.fire(); },
    };
    add(tab);
    select(tab);
    return panel;
  },

  /** A tree view; a row's command goes through `run`, where the page's own commands go. */
  registerTree(id: string, provider: unknown, run: (command: string, args: unknown[]) => Promise<unknown>) {
    return treeView(id, provider as Provider<unknown>, (command, args) => {
      run(command, args).catch((e) => request("log", { text: `${e}\n` }));
    });
  },
};
