// The settings panel: every setting of the registry (spec/settings.json), grouped, with what it
// takes; a change is written by the editor into its own settings store, and the backend's resolution
// (rejections, renamed keys) is shown beside it. Presets apply several at once.
(function (root) {
  "use strict";
  const ui = root.McppUi;
  const { h, t } = ui;

  function control(entry, value, write) {
    if (entry.type === "boolean")
      return h("input", { type: "checkbox", checked: !!value, onchange: (e) => write(e.target.checked) });
    if (entry.enum)
      return h("select", { onchange: (e) => write(e.target.value) }, entry.enum.map((v) => h("option", { value: v, selected: v === value }, v)));
    if (entry.type === "number")
      return h("input", { type: "number", value: value ?? "", min: entry.minimum, max: entry.maximum, onchange: (e) => write(Number(e.target.value)) });
    if (entry.type === "array")
      return h("textarea", { rows: 2, placeholder: t("one per line"), onchange: (e) => write(e.target.value.split("\n").map((s) => s.trim()).filter(Boolean)) },
        (value || []).join("\n"));
    return h("input", { type: "text", value: value ?? "", onchange: (e) => write(e.target.value) });
  }

  ui.panels.settings = {
    title: () => t("Settings"),
    mount(el) {
      const filter = h("input", { type: "search", placeholder: t("Filter settings") });
      const advanced = h("input", { type: "checkbox" });
      const body = h("div");
      let registry, values = {}, resolved = {};

      async function load() {
        ui.clear(body).appendChild(h("p.muted", t("Loading…")));
        try {
          [registry, values] = await Promise.all([ui.rpc("settings/registry", {}), ui.host("settings", {})]);
          resolved = await ui.rpc("settings/update", { settings: values });
        } catch (e) { ui.clear(body).appendChild(ui.errorBox(e)); return; }
        draw();
      }

      function draw() {
        ui.clear(body);
        const needle = filter.value.toLowerCase();
        const rejected = new Map((resolved.rejected || []).map((r) => [r.key, r]));
        const groups = new Map();
        for (const s of registry.settings) {
          if (s.tier === "advanced" && !advanced.checked) continue;
          if (needle && !`${s.key} ${s.title} ${s.description}`.toLowerCase().includes(needle)) continue;
          if (!groups.has(s.group)) groups.set(s.group, []);
          groups.get(s.group).push(s);
        }
        if ((resolved.renames || []).length)
          body.appendChild(ui.section(t("Renamed settings"),
            h("ul.list", resolved.renames.map((r) => h("li", h("code", r.from), " → ", h("code", r.to)))),
            ui.button(t("Move them"), async () => { await ui.host("migrateSettings", { renames: resolved.renames }); load(); })));
        const groupTitle = new Map((registry.groups || []).map((g) => [g.id, g.title]));
        for (const [group, entries] of groups) {
          body.appendChild(ui.section(groupTitle.get(group) || group, entries.map((s) => {
            const value = Object.prototype.hasOwnProperty.call(values, s.key) ? values[s.key] : s.default;
            const bad = rejected.get(s.key);
            return h("div.setting",
              h("label", h("strong", s.title || s.key), h("code.muted", ` ${s.key}`)),
              h("p.muted", s.description),
              control(s, value, async (v) => {
                await ui.host("setSetting", { key: s.key, value: v });
                values[s.key] = v;
                resolved = await ui.rpc("settings/update", { settings: values });
                draw();
              }),
              s.applies === "restart" ? h("p.note", t("Takes effect after a restart.")) : null,
              bad ? h("p.error", t("Not accepted ({0}); expected {1}. The default is used.", bad.reason, bad.expectation)) : null);
          })));
        }
        if ((registry.presets || []).length)
          body.appendChild(ui.section(t("Presets"), h("div.actions.wrap", registry.presets.map((p) =>
            ui.button(p.title || p.id, async () => {
              const preset = await ui.rpc("settings/preset", { id: p.id });
              const entries = Object.entries(preset.values || {});
              // An empty preset is every setting at its default.
              if (!entries.length) await ui.host("resetSettings", { keys: registry.settings.map((s) => s.key) });
              for (const [k, v] of entries) await ui.host("setSetting", { key: k, value: v });
              load();
            }, { secondary: true, title: p.description || "" })))));
      }
      filter.addEventListener("input", ui.debounce(draw, 150));
      advanced.addEventListener("change", draw);
      el.append(h("header.panel-head", h("h1", t("Settings")), h("div.toolbar", filter, h("label", advanced, ` ${t("Show advanced settings")}`))), body);
      load();
    },
  };
})(typeof window !== "undefined" ? window : globalThis);
