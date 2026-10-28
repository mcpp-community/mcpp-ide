// The panel's start: tell the host it is ready, receive which panel to show and with what (the
// project root, a graph kind, a package id), the language's strings and the editor's look, then
// mount it. A later `init` replaces the panel (an editor reuses one view for several).
(function (root) {
  "use strict";
  const ui = root.McppUi;
  let unmount = null;
  ui.on("$init", (m) => {
    ui.setBundle(m.bundle || {});
    if (m.theme && typeof m.theme === "object")
      for (const [k, v] of Object.entries(m.theme)) root.document.documentElement.style.setProperty(k, v);
    const el = root.document.getElementById("app");
    if (typeof unmount === "function") unmount();
    ui.clear(el);
    const panel = ui.panels[m.panel];
    if (!panel) { el.appendChild(ui.empty(ui.t("Unknown panel {0}", m.panel))); return; }
    root.document.title = panel.title(m.params || {});
    unmount = panel.mount(el, m.params || {}) || null;
  });
  if (root.document.readyState === "loading") root.document.addEventListener("DOMContentLoaded", ui.ready);
  else ui.ready();
})(typeof window !== "undefined" ? window : globalThis);
