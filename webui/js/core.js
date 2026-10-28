// The panels' shared helpers: the namespace, the runtime strings, formatting, and a small DOM
// builder. Classic scripts on one global (`McppUi`), so the same files load in a VS Code webview and
// in a JCEF page opened from disk, with no bundler between the repository and either editor.
(function (root) {
  "use strict";
  const ui = (root.McppUi = root.McppUi || /** @type {McppUiApi} */ ({}));
  ui.panels = ui.panels || {};

  // The runtime strings: English keys, the bundle the backend answers for the session's language.
  let bundle = {};
  ui.setBundle = (b) => { bundle = b && typeof b === "object" ? b : {}; };
  ui.t = (key, ...args) => {
    let text = Object.prototype.hasOwnProperty.call(bundle, key) ? bundle[key] : key;
    args.forEach((a, i) => { text = text.split(`{${i}}`).join(String(a)); });
    return text;
  };

  ui.formatBytes = (bytes) => {
    const units = ["B", "KiB", "MiB", "GiB", "TiB"];
    let v = Math.max(0, Number(bytes) || 0);
    let u = 0;
    while (v >= 1024 && u + 1 < units.length) { v /= 1024; ++u; }
    return u === 0 ? `${Math.round(v)} B` : `${v.toFixed(1)} ${units[u]}`;
  };

  // "3 days ago" from seconds (or milliseconds, when the value is that large) since the epoch.
  ui.ago = (when, now = Date.now()) => {
    if (when === undefined || when === null) return "";
    let ms = Number(when);
    if (ms < 1e12) ms *= 1000;
    const s = Math.max(0, Math.round((now - ms) / 1000));
    if (s < 60) return ui.t("just now");
    if (s < 3600) return ui.t("{0} min ago", Math.round(s / 60));
    if (s < 86400) return ui.t("{0} h ago", Math.round(s / 3600));
    return ui.t("{0} days ago", Math.round(s / 86400));
  };

  ui.debounce = (fn, ms) => {
    let timer;
    return (...args) => { clearTimeout(timer); timer = setTimeout(() => fn(...args), ms); };
  };

  // h("div.row#id", {onclick, title, ...}, children...) -- children are nodes, strings or arrays.
  ui.h = (spec, props, ...children) => {
    const doc = root.document;
    const m = /^([a-z0-9-]+)?((?:[.#][\w-]+)*)$/i.exec(spec) || [];
    const el = doc.createElement(m[1] || "div");
    for (const part of (m[2] || "").match(/[.#][\w-]+/g) || []) {
      if (part[0] === ".") el.classList.add(part.slice(1)); else el.id = part.slice(1);
    }
    if (props && (typeof props !== "object" || props.nodeType || Array.isArray(props))) { children.unshift(props); props = null; }
    for (const [k, v] of Object.entries(props || {})) {
      if (v === undefined || v === null || v === false) continue;
      if (k.startsWith("on") && typeof v === "function") el.addEventListener(k.slice(2), v);
      else if (k === "class") el.className = v;
      else if (k === "style" && typeof v === "object") Object.assign(el.style, v);
      else if (k === "dataset") Object.assign(el.dataset, v);
      else if (k in el && typeof v !== "string") el[k] = v;
      else el.setAttribute(k, v === true ? "" : String(v));
    }
    const add = (c) => {
      if (c === undefined || c === null || c === false) return;
      if (Array.isArray(c)) { c.forEach(add); return; }
      el.appendChild(c.nodeType ? c : doc.createTextNode(String(c)));
    };
    children.forEach(add);
    return el;
  };

  ui.clear = (el) => { while (el.firstChild) el.removeChild(el.firstChild); return el; };

  // A status badge: ok | warning | error | info | running.
  ui.badge = (state, text) => ui.h(`span.badge.badge-${state}`, text);

  ui.button = (text, onclick, opts = {}) =>
    ui.h(`button${opts.secondary ? ".secondary" : ""}`, { onclick, disabled: !!opts.disabled, title: opts.title }, text);

  ui.section = (title, ...children) => ui.h("section.card", ui.h("h2", title), ...children);

  ui.empty = (text) => ui.h("p.empty", text);

  ui.errorBox = (error) =>
    ui.h("div.error", ui.h("strong", ui.t("mcpp could not answer")), ui.h("pre", (error && (error.message || String(error))) || ""));
})(typeof window !== "undefined" ? window : globalThis);
