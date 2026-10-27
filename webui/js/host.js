// The bridge to the editor that hosts the panel. The panel speaks the backend's protocol through
// it (`rpc`), receives the backend's notifications (`on`), and asks the editor for what only the
// editor can do (`host`: open a file, confirm, write a setting, start a terminal).
//
// VS Code: `acquireVsCodeApi().postMessage` out, `window` "message" events in.
// JetBrains (JCEF): `window.mcppHostSend(text)` out (a JBCefJSQuery the plugin injects), and the
// plugin calls `window.mcppHostReceive(message)` in.
(function (root) {
  "use strict";
  const ui = (root.McppUi = root.McppUi || /** @type {McppUiApi} */ ({}));
  let send;
  const vscode = typeof root.acquireVsCodeApi === "function" ? root.acquireVsCodeApi() : undefined;
  if (vscode) send = (m) => vscode.postMessage(m);
  else send = (m) => { if (typeof root.mcppHostSend === "function") root.mcppHostSend(JSON.stringify(m)); else queue.push(m); };
  const queue = [];
  let nextId = 1;
  const pending = new Map();
  const listeners = new Map();

  function receive(m) {
    if (typeof m === "string") { try { m = JSON.parse(m); } catch { return; } }
    if (!m || typeof m !== "object") return;
    if (m.type === "rpcResult" || m.type === "hostResult") {
      const p = pending.get(m.id); pending.delete(m.id);
      if (p) p.resolve(m.result);
    } else if (m.type === "rpcError" || m.type === "hostError") {
      const p = pending.get(m.id); pending.delete(m.id);
      if (p) p.reject(Object.assign(new Error((m.error && m.error.message) || "failed"), { data: m.error && m.error.data, code: m.error && m.error.code }));
    } else if (m.type === "notify") {
      for (const fn of listeners.get(m.method) || []) fn(m.params);
      for (const fn of listeners.get("*") || []) fn(m.method, m.params);
    } else if (m.type === "init") {
      for (const fn of listeners.get("$init") || []) fn(m);
    }
  }
  if (vscode) root.addEventListener("message", (e) => receive(e.data));
  root.mcppHostReceive = receive;
  // The JCEF query is injected after the page loads: whatever was sent before it goes then.
  root.mcppHostAttached = () => { while (queue.length) send(queue.shift()); };

  function request(type, fields) {
    const id = nextId++;
    return new Promise((resolve, reject) => {
      pending.set(id, { resolve, reject });
      send(Object.assign({ type, id }, fields));
    });
  }

  ui.rpc = (method, params = {}) => request("rpc", { method, params });
  ui.host = (action, params = {}) => request("host", { action, params });
  ui.on = (method, fn) => {
    if (!listeners.has(method)) listeners.set(method, new Set());
    listeners.get(method).add(fn);
    return () => listeners.get(method).delete(fn);
  };
  ui.ready = () => send({ type: "ready" });
  ui.state = {
    get: () => (vscode ? vscode.getState() : root.__mcppState) || {},
    set: (s) => { if (vscode) vscode.setState(s); else root.__mcppState = s; },
  };
  ui.editor = vscode ? "vscode" : "jetbrains";
})(typeof window !== "undefined" ? window : globalThis);
