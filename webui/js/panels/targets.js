// The targets panel: every target mcpp knows, the toolchains installed for it, and the platform
// that configures it (mcpp's platforms: a built-in convention, a pinned platform package, or one the
// project or this machine chose), with each of the platform's fields and where its value came from.
// Choosing a target writes `mcpp.build.target`; build, run, test, debug and pack then use it.
(function (root) {
  "use strict";
  const ui = root.McppUi;
  const { h, t } = ui;

  function fields(platform) {
    const f = (platform && platform.fields) || {};
    const keys = Object.keys(f);
    if (!keys.length) return null;
    return h("table.fields", keys.map((k) => {
      const v = f[k] && Object.prototype.hasOwnProperty.call(f[k], "value") ? f[k].value : f[k];
      return h("tr", h("td", h("code", k)), h("td", typeof v === "string" ? v : JSON.stringify(v)), h("td.muted", (f[k] && f[k].source) || ""));
    }));
  }

  ui.panels.targets = {
    title: () => t("Targets and Platforms"),
    mount(el, params) {
      const rootDir = params.root;
      async function render() {
        ui.clear(el).appendChild(h("p.muted", t("Loading…")));
        let r;
        try { r = await ui.rpc("targets/list", { root: rootDir }); } catch (e) { ui.clear(el).appendChild(ui.errorBox(e)); return; }
        ui.clear(el);
        el.appendChild(h("header.panel-head",
          h("h1", t("Targets and Platforms")),
          h("p", t("This machine: {0}", r.host), r.hostPlatformText ? h("span.muted", ` · ${t("host platform")}: ${r.hostPlatformText}`) : null),
          r.lastPlan && r.lastPlan.configuration ? h("p.muted", t("Last build: {0}, {1}", r.lastPlan.configuration.triple, r.lastPlan.platformText || t("host conventions"))) : null));
        const rows = r.rows.map((row) => {
          const details = h("details",
            h("summary", row.platformText || t("platform not reported by this mcpp")),
            fields(row.platform) || h("p.muted", t("No platform fields reported.")));
          const choose = row.selected ? ui.badge("ok", t("selected"))
            : ui.button(t("Use"), async () => { await ui.host("setSetting", { key: "mcpp.build.target", value: row.host ? "" : row.target }); render(); }, { secondary: true });
          const toolchains = row.toolchains.length
            ? row.toolchains.map((tc) => h("div", tc.spec || "—", " ", ui.badge(tc.status === "installed" ? "ok" : "info", tc.status), tc.default ? ` ${t("(default)")}` : ""))
            : h("span.muted", "—");
          const install = !row.installed && row.pin
            ? ui.button(t("Install"), () => ui.rpc("toolchains/install", { spec: row.pin, target: row.target, root: rootDir }), { secondary: true })
            : null;
          return h(`tr${row.selected ? ".selected" : ""}`,
            h("td", h("strong", row.target), row.host ? h("span.muted", ` ${t("(this machine)")}`) : null, row.note ? h("div.muted", row.note) : null),
            h("td", toolchains, install),
            h("td", details),
            h("td", choose));
        });
        el.appendChild(h("table.targets",
          h("thead", h("tr", h("th", t("Target")), h("th", t("Toolchains")), h("th", t("Platform")), h("th", ""))),
          h("tbody", rows)));
      }
      const offs = [
        ui.on("job/finished", (p) => { if (p.kind === "toolchain") render(); }),
        ui.on("job/plan", (p) => { if (p.root === rootDir) render(); }),
      ];
      render();
      return () => offs.forEach((off) => off());
    },
  };
})(typeof window !== "undefined" ? window : globalThis);
