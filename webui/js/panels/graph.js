// The dependency graphs: packages (which set sees which) and modules (who provides, who imports),
// from mcpp's build database through the backend's view models. Drawn as SVG with a layered layout;
// a large graph opens on the neighbourhood of one node, chosen by search or by a click.
(function (root) {
  "use strict";
  const ui = root.McppUi;
  const { h, t } = ui;
  const SVG = "http://www.w3.org/2000/svg";
  const svg = (tag, attrs = {}, ...children) => {
    const el = root.document.createElementNS(SVG, tag);
    for (const [k, v] of Object.entries(attrs)) el.setAttribute(k, String(v));
    children.forEach((c) => c && el.appendChild(c));
    return el;
  };
  // Beyond this many nodes the whole graph is shown only when asked for.
  const WHOLE_LIMIT = 150;

  ui.panels.graph = {
    title: (params) => (params.graph === "modules" ? t("Module graph") : t("Dependency graph")),
    mount(el, params) {
      const kind = params.graph === "modules" ? "modules" : "packages";
      const rootDir = params.root;
      let data = { nodes: [], edges: [] };
      let focus = null, depth = 1, whole = false;
      let zoom = 1, panX = 0, panY = 0;
      const search = h("input", { type: "search", placeholder: t("Find a node") });
      const list = h("datalist#graph-nodes");
      search.setAttribute("list", "graph-nodes");
      const depthSel = h("select", [1, 2, 3].map((d) => h("option", { value: d }, t("{0} step(s) around", d))));
      const canvas = h("div.graph-canvas");
      const status = h("p.muted");

      function draw() {
        ui.clear(canvas);
        let view = data;
        if (focus) view = ui.graphLayout.neighbourhood(data.nodes, data.edges, focus, depth);
        else if (data.nodes.length > WHOLE_LIMIT && !whole) {
          status.textContent = t("{0} nodes: choose one to see its neighbourhood, or show the whole graph.", data.nodes.length);
          canvas.appendChild(h("div.actions", ui.button(t("Show the whole graph"), () => { whole = true; draw(); }, { secondary: true })));
          return;
        }
        status.textContent = focus ? t("{0} and {1} node(s) around it", focus, view.nodes.length - 1)
                                   : t("{0} nodes, {1} edges", view.nodes.length, view.edges.length);
        const lay = ui.graphLayout.layout(view.nodes, view.edges, { nodeWidth: kind === "modules" ? 200 : 180 });
        const g = svg("g");
        const defs = svg("defs", {}, svg("marker", { id: "arrow", viewBox: "0 0 10 10", refX: 9, refY: 5, markerWidth: 6, markerHeight: 6, orient: "auto-start-reverse" },
          svg("path", { d: "M 0 0 L 10 5 L 0 10 z", class: "arrow" })));
        for (const e of lay.edges) {
          const [[x1, y1], [x2, y2]] = e.points;
          const my = (y1 + y2) / 2;
          g.appendChild(svg("path", { d: `M ${x1} ${y1} C ${x1} ${my}, ${x2} ${my}, ${x2} ${y2}`, class: e.back ? "edge back" : "edge", "marker-end": "url(#arrow)" }));
        }
        for (const n of lay.nodes) {
          const label = n.id.length > 28 ? `${n.id.slice(0, 26)}…` : n.id;
          const node = svg("g", { class: `node kind-${n.kind || "set"}${n.id === focus ? " focus" : ""}`, transform: `translate(${n.x},${n.y})`, tabindex: 0 },
            svg("rect", { width: n.width, height: n.height, rx: 4 }),
            svg("text", { x: 8, y: n.height / 2 + 4 }, root.document.createTextNode(label)));
          const title = svg("title");
          title.textContent = [n.id, n.source || "", n.set ? `(${n.set})` : ""].filter(Boolean).join("\n");
          node.appendChild(title);
          node.addEventListener("click", () => { focus = n.id; draw(); });
          node.addEventListener("dblclick", () => { if (n.source) ui.host("openFile", { path: n.source }); });
          node.addEventListener("keydown", (ev) => { if (ev.key === "Enter") { focus = n.id; draw(); } });
          g.appendChild(node);
        }
        const s = svg("svg", { width: "100%", height: "100%" }, defs, g);
        const apply = () => g.setAttribute("transform", `translate(${panX},${panY}) scale(${zoom})`);
        zoom = Math.min(1, (canvas.clientWidth || 800) / lay.width, (canvas.clientHeight || 600) / lay.height) || 1;
        panX = Math.max(0, ((canvas.clientWidth || 800) - lay.width * zoom) / 2); panY = 8;
        apply();
        s.addEventListener("wheel", (ev) => {
          ev.preventDefault();
          const next = Math.max(0.05, Math.min(4, zoom * (ev.deltaY < 0 ? 1.1 : 1 / 1.1)));
          const r = s.getBoundingClientRect(), x = ev.clientX - r.left, y = ev.clientY - r.top;
          panX = x - ((x - panX) * next) / zoom; panY = y - ((y - panY) * next) / zoom; zoom = next; apply();
        }, { passive: false });
        let drag = null;
        s.addEventListener("pointerdown", (ev) => { if (ev.target === s) { drag = [ev.clientX - panX, ev.clientY - panY]; s.setPointerCapture(ev.pointerId); } });
        s.addEventListener("pointermove", (ev) => { if (drag) { panX = ev.clientX - drag[0]; panY = ev.clientY - drag[1]; apply(); } });
        s.addEventListener("pointerup", () => { drag = null; });
        canvas.appendChild(s);
      }

      async function load() {
        status.textContent = t("Loading…");
        try { data = await ui.rpc(kind === "modules" ? "graph/modules" : "graph/packages", { root: rootDir }); }
        catch (e) { status.textContent = ""; ui.clear(canvas).appendChild(ui.errorBox(e)); return; }
        ui.clear(list);
        data.nodes.forEach((n) => list.appendChild(h("option", { value: n.id })));
        if (params.focus) focus = params.focus;
        draw();
      }
      search.addEventListener("change", () => { if (data.nodes.some((n) => n.id === search.value)) { focus = search.value; draw(); } });
      depthSel.addEventListener("change", () => { depth = Number(depthSel.value); draw(); });
      el.append(
        h("header.panel-head",
          h("h1", kind === "modules" ? t("Module graph") : t("Dependency graph")),
          h("div.toolbar", search, list, depthSel,
            ui.button(t("Overview"), () => { focus = null; draw(); }, { secondary: true }),
            ui.button(t("Refresh"), load, { secondary: true }),
            ui.button(t("Copy as Mermaid"), () => {
              const lines = ["graph TD"];
              data.edges.forEach((e) => lines.push(`  ${JSON.stringify(e.from)} --> ${JSON.stringify(e.to)}`.replace(/"/g, "")));
              ui.host("copy", { text: lines.join("\n") });
            }, { secondary: true })),
          status),
        canvas);
      const off = ui.on("result/changed", (p) => { if (p.root === rootDir) load(); });
      load();
      return off;
    },
  };
})(typeof window !== "undefined" ? window : globalThis);
