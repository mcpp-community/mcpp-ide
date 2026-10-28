// The project panel: what the project builds (executables, tests, libraries), for which target and
// platform, how its last build went, and the language service that answers its sources. The actions
// are the command table's: the editor runs them as it runs the same commands from its menus.
(function (root) {
  "use strict";
  const ui = root.McppUi;
  const { h, t } = ui;

  function languageServiceCard(status, refresh) {
    if (!status) return null;
    const state = status.state === "ok" ? "ok" : status.state === "missing" ? "error" : "warning";
    return ui.section(
      t("C++ Modules language service"),
      h("p", ui.badge(state, status.state === "ok" ? `mcppls ${status.version}` : t("needs attention")), " ", status.message),
      h("div.actions",
        (status.actions || []).map((a) => ui.button(a.title, () => ui.host("languageService", { action: a.id }))),
        ui.button(t("Check again"), refresh, { secondary: true })));
  }

  function list(title, rows, render) {
    return ui.section(`${title} (${rows.length})`, rows.length ? h("ul.list", rows.map(render)) : ui.empty(t("none")));
  }

  ui.panels.project = {
    title: () => t("Project"),
    mount(el, params) {
      const rootDir = params.root;
      let jobRunning = null;
      const command = (id) => () => ui.host("runCommand", { command: id, root: rootDir });

      async function render() {
        ui.clear(el);
        if (!rootDir) { el.appendChild(ui.empty(t("No mcpp project in this workspace"))); return; }
        el.appendChild(h("p.muted", t("Loading…")));
        let summary, ls;
        try {
          [summary, ls] = await Promise.all([ui.rpc("project/summary", { root: rootDir }), ui.rpc("languageService/status", {})]);
        } catch (e) {
          ui.clear(el).appendChild(ui.errorBox(e));
          return;
        }
        ui.clear(el);
        const name = rootDir.split(/[\\/]/).filter(Boolean).pop();
        const target = summary.target || "";
        el.appendChild(h("header.panel-head",
          h("h1", name),
          h("p.muted", rootDir),
          h("div.actions",
            ui.button(t("Build"), command("mcpp.build"), { disabled: !!jobRunning }),
            ui.button(t("Run"), command("mcpp.run")),
            ui.button(t("Debug"), command("mcpp.debug")),
            ui.button(t("Test"), command("mcpp.test"), { disabled: !!jobRunning }),
            ui.button(t("Pack"), () => ui.host("openPanel", { panel: "pack", root: rootDir }), { secondary: true }),
            ui.button(t("Clean target"), command("mcpp.clean"), { secondary: true }),
            jobRunning ? ui.button(t("Cancel"), () => ui.rpc("job/cancel", { jobId: jobRunning }), { secondary: true }) : null)));

        const facts = h("dl.facts",
          h("dt", t("Target")), h("dd", target || t("this machine"), " ", ui.h("a", { href: "#", onclick: (e) => { e.preventDefault(); ui.host("openPanel", { panel: "targets", root: rootDir }); } }, t("change"))),
          h("dt", t("Platform")), h("dd", summary.platformText || t("reported after the first build")),
          summary.hostPlatformText ? [h("dt", t("Host platform")), h("dd", summary.hostPlatformText)] : null,
          h("dt", t("Toolchain")), h("dd", summary.toolchain || "—"));
        if (summary.lastBuild) {
          const b = summary.lastBuild;
          facts.append(h("dt", t("Last build")),
            h("dd", ui.badge(b.status === "ok" ? "ok" : b.status === "cancelled" ? "info" : "error", b.status), " ",
              ui.ago(b.finishedAt), b.client ? ` · ${b.client}` : ""));
        }
        el.appendChild(ui.section(t("Overview"), facts, summary.error ? ui.errorBox({ message: summary.error }) : null));
        el.appendChild(list(t("Executables"), summary.targets || [], (r) =>
          h("li", h("span", r.name), h("span.muted", r.configuration || ""))));
        el.appendChild(list(t("Tests"), summary.tests || [], (r) =>
          h("li", h("span", r.name), ui.button(t("Run"), () => ui.host("runCommand", { command: "mcpp.test", root: rootDir, args: { filter: r.name } }), { secondary: true }))));
        el.appendChild(list(t("Libraries"), summary.libraries || [], (r) => h("li", h("span", r.name), h("span.muted", r.kind))));
        el.appendChild(h("div.actions",
          ui.button(t("Dependency graph"), () => ui.host("openPanel", { panel: "graph", root: rootDir, graph: "packages" }), { secondary: true }),
          ui.button(t("Module graph"), () => ui.host("openPanel", { panel: "graph", root: rootDir, graph: "modules" }), { secondary: true }),
          ui.button(t("Libraries"), () => ui.host("openPanel", { panel: "libraries", root: rootDir }), { secondary: true })));
        const card = languageServiceCard(ls, render);
        if (card) el.appendChild(card);
      }

      const offs = [
        ui.on("job/started", (p) => { if (p.root === rootDir) { jobRunning = p.jobId; render(); } }),
        ui.on("job/finished", (p) => { if (p.root === rootDir || p.jobId === jobRunning) { jobRunning = null; render(); } }),
        ui.on("result/changed", (p) => { if (p.root === rootDir) render(); }),
        ui.on("manifest/changed", (p) => { if (p.root === rootDir) render(); }),
      ];
      render();
      return () => offs.forEach((off) => off());
    },
  };
})(typeof window !== "undefined" ? window : globalThis);
