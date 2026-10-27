// The pack panel: the formats mcpp can produce for this project (`mcpp pack --list-formats`, with
// the plugins that provide them), the mode, the target, and the result of a pack: what was written
// and where.
(function (root) {
  "use strict";
  const ui = root.McppUi;
  const { h, t } = ui;

  ui.panels.pack = {
    title: () => t("Pack"),
    mount(el, params) {
      const rootDir = params.root;
      const format = h("select");
      const mode = h("select", ["", "static", "bundle-project", "bundle-all", "self-contained"].map((m) =>
        h("option", { value: m }, m || t("the project's default"))));
      const profile = h("select", ["release", "dev"].map((p) => h("option", { value: p }, p)));
      const target = h("input", { type: "text", placeholder: t("this machine") });
      const output = h("pre.log");
      const result = h("div");
      let jobId = null;
      const go = ui.button(t("Pack"), async () => {
        ui.clear(output); ui.clear(result);
        try {
          const r = await ui.rpc("pack/start", { root: rootDir, format: format.value || undefined, mode: mode.value || undefined,
                                                 profile: profile.value, target: target.value });
          jobId = r.jobId; go.disabled = true;
        } catch (e) { result.appendChild(ui.errorBox(e)); }
      });
      const offs = [
        ui.on("job/output", (p) => { if (p.jobId === jobId) output.textContent += p.text; }),
        ui.on("job/finished", (p) => {
          if (p.jobId !== jobId) return;
          go.disabled = false; jobId = null;
          ui.clear(result);
          const data = (p.envelope && p.envelope.data) || {};
          result.appendChild(ui.section(t("Result"),
            h("p", ui.badge(p.status === "ok" ? "ok" : "error", p.status)),
            (p.diagnostics || []).map((d) => h("p.error", d.message)),
            data.outputs || data.artifacts ? h("ul.list", (data.outputs || data.artifacts).map((o) => {
              const path = typeof o === "string" ? o : o.path;
              return h("li", h("code", path), ui.button(t("Reveal"), () => ui.host("reveal", { path }), { secondary: true }));
            })) : h("pre", JSON.stringify(data, null, 2))));
        }),
      ];
      el.append(h("header.panel-head", h("h1", t("Pack")), h("p.muted", rootDir || "")),
        ui.section(t("What to produce"),
          h("dl.facts",
            h("dt", t("Format")), h("dd", format),
            h("dt", t("Mode")), h("dd", mode),
            h("dt", t("Profile")), h("dd", profile),
            h("dt", t("Target")), h("dd", target)),
          h("div.actions", go)),
        result, output);
      ui.rpc("pack/formats", { root: rootDir }).then((r) => {
        format.appendChild(h("option", { value: "" }, t("the project's default")));
        for (const f of (r && r.formats) || [])
          format.appendChild(h("option", { value: f.name }, f.builtin ? f.name : `${f.name} (${(f.providers || []).join(", ")})`));
      }, (e) => result.appendChild(ui.errorBox(e)));
      ui.host("settings", {}).then((s) => { if (s && s["mcpp.build.target"]) target.value = s["mcpp.build.target"]; });
      return () => offs.forEach((off) => off());
    },
  };
})(typeof window !== "undefined" ? window : globalThis);
