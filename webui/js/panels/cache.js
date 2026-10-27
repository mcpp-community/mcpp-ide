// The cache panel: what mcpp's shared build cache holds (`mcpp cache list`), by kind, by package and
// by age, and the cleanups. A cleanup is a plan the backend makes (its argv, its danger level, its
// preview); the editor confirms it natively, at the level the plan asks for.
(function (root) {
  "use strict";
  const ui = root.McppUi;
  const { h, t } = ui;

  // plan -> preview -> the editor's confirmation -> apply. Used by the panel and nothing else: the
  // editors' own commands run the same three steps against the same plan.
  async function cleanup(action, rootDir) {
    const plan = await ui.rpc("cache/plan", { action, root: rootDir });
    if (plan.refusedUntrusted) {
      await ui.host("notify", { level: "warning", message: t("this action needs a trusted workspace") });
      return false;
    }
    let confirmation = {};
    if (plan.level > 0) {
      let preview = "";
      if (plan.preview && plan.preview.source === "mcpp") preview = plan.preview.text || "";
      else if (plan.preview && plan.preview.entries)
        preview = t("About {0} would be freed ({1} entries).", plan.preview.bytesText, plan.preview.count);
      const answer = await ui.host("confirm", { title: plan.title, detail: plan.detail, preview, level: plan.level, acknowledge: plan.acknowledge });
      if (!answer || !answer.confirmed) return false;
      confirmation = { confirmed: true, previewShown: !!plan.preview, acknowledged: !!answer.acknowledged };
    }
    await ui.rpc("cache/apply", { action, root: rootDir, confirmation });
    return true;
  }
  ui.cleanup = cleanup;

  function bars(rows, total, label) {
    return h("table.bars", rows.map((r) => h("tr",
      h("td", label(r)),
      h("td.num", ui.formatBytes(r.bytes)),
      h("td.bar", h("div", { style: { width: `${total ? Math.max(1, Math.round((100 * r.bytes) / total)) : 0}%` } })))));
  }

  ui.panels.cache = {
    title: () => t("Cache"),
    mount(el, params) {
      const rootDir = params.root;
      async function render() {
        ui.clear(el).appendChild(h("p.muted", t("Loading…")));
        let inv;
        try { inv = await ui.rpc("cache/inventory", { root: rootDir }); } catch (e) { ui.clear(el).appendChild(ui.errorBox(e)); return; }
        ui.clear(el);
        el.append(
          h("header.panel-head",
            h("h1", t("Cache")),
            h("p", h("strong", inv.totalText), " ", t("in {0} entries", inv.totalEntries), inv.root ? h("span.muted", ` · ${inv.root}`) : null),
            h("div.actions", ui.button(t("Refresh"), render, { secondary: true }))),
          ui.section(t("By kind"), bars(inv.byKind, inv.totalBytes, (r) => `${r.kind} (${r.entries})`)),
          ui.section(t("Largest packages"), inv.topLabels.length ? bars(inv.topLabels, inv.totalBytes, (r) => r.label) : ui.empty(t("none"))),
          ui.section(t("By last use"), bars(inv.ageBuckets, inv.totalBytes, (b) =>
            b.toDays === undefined ? t("older than {0} days", b.fromDays) : t("{0}–{1} days", b.fromDays, b.toDays))),
          inv.incomplete.length ? ui.section(t("Incomplete entries"), h("ul.list", inv.incomplete.map((e) => h("li", e.label || e.key)))) : null,
          ui.section(t("Clean up"),
            h("div.actions.wrap",
              [["stale", t("Clean stale artifacts")], ["project", t("Clean project artifacts")], ["cacheGc", t("Collect to a budget")],
               ["cachePrune", t("Prune by age")], ["cacheDeps", t("Drop package entries")], ["cacheStd", t("Drop standard library entries")],
               ["cacheVerify", t("Verify")], ["cacheLegacy", t("Remove the pre-v1 cache")], ["cacheAll", t("Drop everything")]]
                .map(([action, label]) => ui.button(label, async () => {
                  try { if (await cleanup(action, rootDir)) setTimeout(render, 500); } catch (e) { el.prepend(ui.errorBox(e)); }
                }, { secondary: action !== "stale", disabled: action === "project" && !rootDir })))));
      }
      const off = ui.on("job/finished", (p) => { if (p.kind === "clean" || p.kind === "command") render(); });
      render();
      return off;
    },
  };
})(typeof window !== "undefined" ? window : globalThis);
