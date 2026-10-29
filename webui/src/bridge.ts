/**
 * The bridge to the IDE that hosts the workbench (mcpp-clion's JCEF browser).
 *
 * Asynchronous requests travel through `window.mcppHostSend(text)` (a JBCefJSQuery the plugin
 * injects) and come back through `window.mcppHostReceive(message)`; the IDE's notifications arrive
 * the same way. A synchronous request -- what a synchronous file read of the page code needs -- is
 * a `prompt()` the plugin answers through its JS dialog handler, which is the one synchronous
 * channel a Chromium page has to its host.
 */

type Pending = { resolve: (value: unknown) => void; reject: (error: Error) => void };

const pending = new Map<number, Pending>();
const listeners = new Map<string, Set<(params: any) => void>>();
const queue: string[] = [];
let nextId = 1;

declare global {
  interface Window {
    mcppHostSend?: (text: string) => void;
    mcppHostReceive?: (message: unknown) => void;
    mcppHostAttached?: () => void;
  }
}

function send(message: unknown): void {
  const text = JSON.stringify(message);
  if (typeof window.mcppHostSend === "function") window.mcppHostSend(text);
  else queue.push(text);
}

window.mcppHostAttached = () => {
  while (queue.length > 0) window.mcppHostSend?.(queue.shift()!);
};

window.mcppHostReceive = (raw: unknown) => {
  let message = raw as { type?: string; id?: number; result?: unknown; error?: { message?: string; code?: number; data?: unknown }; method?: string; params?: unknown };
  if (typeof raw === "string") {
    try { message = JSON.parse(raw); } catch { return; }
  }
  if (!message || typeof message !== "object") return;
  if (message.type === "result" || message.type === "error") {
    const p = pending.get(message.id ?? -1);
    pending.delete(message.id ?? -1);
    if (!p) return;
    if (message.type === "result") p.resolve(message.result);
    else p.reject(Object.assign(new Error(message.error?.message ?? "failed"), { code: message.error?.code, data: message.error?.data }));
  } else if (message.type === "notify" && typeof message.method === "string") {
    for (const fn of listeners.get(message.method) ?? []) fn(message.params);
    for (const fn of listeners.get("*") ?? []) fn({ method: message.method, params: message.params });
  }
};

/** An action of the IDE: `rpc` reaches the backend, everything else the IDE itself. */
export function request<T = unknown>(action: string, params: unknown = {}): Promise<T> {
  const id = nextId++;
  return new Promise<T>((resolve, reject) => {
    pending.set(id, { resolve: resolve as (value: unknown) => void, reject });
    send({ type: "request", id, action, params });
  });
}

/** A method of the backend (`mcpp-ide serve`). */
export function rpc<T = unknown>(method: string, params: unknown = {}): Promise<T> {
  return request<T>("rpc", { method, params });
}

/** The IDE's answer, now: for the synchronous file reads of the page code. */
export function requestSync<T = unknown>(action: string, params: unknown = {}): T {
  const answer = window.prompt(`mcpp-host:${JSON.stringify({ action, params })}`, "");
  if (answer === null) throw new Error(`the IDE did not answer ${action}`);
  const parsed = JSON.parse(answer) as { ok: boolean; result?: T; error?: string; code?: string };
  if (!parsed.ok) throw Object.assign(new Error(parsed.error ?? `${action} failed`), { code: parsed.code });
  return parsed.result as T;
}

/** The IDE's notifications (`*` for every one): the backend's, and the IDE's own. */
export function on(method: string, fn: (params: any) => void): { dispose(): void } {
  if (!listeners.has(method)) listeners.set(method, new Set());
  listeners.get(method)!.add(fn);
  return { dispose: () => listeners.get(method)?.delete(fn) };
}

/** What the IDE told the workbench when it loaded. */
export interface HostInfo {
  /** "linux" | "darwin" | "win32" */
  platform: string;
  arch: string;
  /** The IDE's language: "en" | "zh-cn" | ... */
  language: string;
  home: string;
  tmp: string;
  env: Record<string, string>;
  /** The workbench's own files (media/, dist/vendor/) as a URL. */
  extensionUri: string;
  workspaceFolders: string[];
  trusted: boolean;
}

export let info: HostInfo = {
  platform: "linux", arch: "x64", language: "en", home: "/", tmp: "/tmp", env: {}, extensionUri: "", workspaceFolders: [], trusted: false,
};

export function setInfo(next: HostInfo): void {
  info = next;
}
