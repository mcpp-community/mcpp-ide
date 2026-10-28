// The libraries panel: the packages of every index mcpp reads (`mcpp search`), filtered and sorted
// by the backend, and one package's page. "Add" runs `mcpp add` on the project, which writes
// mcpp.toml; nothing here edits the manifest itself.
(function (root) {
  "use strict";
  const ui = root.McppUi;
  const { h, t } = ui;

  function detail(el, pkg, rootDir, back) {
    ui.clear(el);
    const versions = pkg.versions || [];
    const select = h("select", versions.map((v) => h("option", { value: v }, v)));
    el.append(
      h("header.panel-head",
        h("a.back", { href: "#", onclick: (e) => { e.preventDefault(); back(); } }, `← ${t("All packages")}`),
        h("h1", pkg.id),
        h("p", pkg.description || ""),
        h("dl.facts",
          h("dt", t("Index")), h("dd", pkg.index || ""),
          h("dt", t("Dependency name")), h("dd", h("code", pkg.dependencyName || pkg.name)),
          h("dt", t("Licences")), h("dd", (pkg.licenses || []).join(", ") || "—"),
          pkg.repo ? [h("dt", t("Repository")), h("dd", h("a", { href: "#", onclick: (e) => { e.preventDefault(); ui.host("openExternal", { url: pkg.repo }); } }, pkg.repo))] : null),
        h("div.actions",
          select,
          ui.button(t("Add to mcpp.toml"), async () => {
            if (!rootDir) { await ui.host("notify", { level: "warning", message: t("No mcpp project in this workspace") }); return; }
            await ui.rpc("library/add", { root: rootDir, dependency: pkg.dependencyName || pkg.name, version: select.value });
          }, { disabled: !rootDir }))),
      ui.section(t("Versions"), h("ul.list", versions.map((v) => h("li", h("code", v))))));
  }

  ui.panels.libraries = {
    title: () => t("Libraries"),
    mount(el, params) {
      const rootDir = params.root;
      const saved = ui.state.get().libraries || {};
      const search = h("input", { type: "search", placeholder: t("Search packages"), value: saved.keyword || "" });
      const sort = h("select", h("option", { value: "name" }, t("by name")), h("option", { value: "versions" }, t("by versions")));
      sort.value = saved.sort || "name";
      const index = h("select", h("option", { value: "" }, t("every index")));
      const results = h("div.results");
      const status = h("p.muted");
      let lastQuery = 0;

      async function query(refresh = false) {
        const id = ++lastQuery;
        ui.state.set(Object.assign(ui.state.get(), { libraries: { keyword: search.value, sort: sort.value } }));
        status.textContent = t("Searching…");
        let r;
        try {
          r = await ui.rpc("library/search", { keyword: search.value, sort: sort.value, index: index.value, limit: 300, refresh, root: rootDir });
        } catch (e) {
          if (id === lastQuery) { status.textContent = ""; ui.clear(results).appendChild(ui.errorBox(e)); }
          return;
        }
        if (id !== lastQuery) return;
        if (index.options.length === 1) for (const name of r.indexes || []) index.appendChild(h("option", { value: name }, name));
        status.textContent = t("{0} packages", r.total);
        ui.clear(results);
        if (!r.packages.length) { results.appendChild(ui.empty(t("No package matches."))); return; }
        results.appendChild(h("ul.list.packages", r.packages.map((p) =>
          h("li.package", { onclick: () => detail(el, p, rootDir, () => { ui.clear(el); mountList(); }) },
            h("div", h("strong", p.id), h("span.muted", ` ${p.latest || ""}`)),
            h("div.muted", p.description || "")))));
      }

      function mountList() {
        el.append(
          h("header.panel-head",
            h("h1", t("Libraries")),
            h("div.toolbar", search, sort, index,
              ui.button(t("Refresh the index"), async () => { await ui.rpc("library/updateIndex", { root: rootDir }); }, { secondary: true }))),
          status, results);
        query();
      }
      search.addEventListener("input", ui.debounce(() => query(), 200));
      sort.addEventListener("change", () => query());
      index.addEventListener("change", () => query());
      const off = ui.on("job/finished", (p) => { if ((p.envelope && p.envelope.kind) || p.kind === "command") query(true); });
      if (params.id) {
        ui.rpc("library/search", { id: params.id, root: rootDir }).then((p) => detail(el, p, rootDir, () => { ui.clear(el); mountList(); }),
          (e) => el.appendChild(ui.errorBox(e)));
      } else mountList();
      return off;
    },
  };
})(typeof window !== "undefined" ? window : globalThis);
