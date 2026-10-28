// The layered layout and the neighbourhood: every edge that is not part of a cycle points down a
// layer, nodes of a layer do not overlap, and a neighbourhood holds exactly the nodes in reach.
const assert = require("node:assert/strict");
const { test } = require("node:test");
const { load } = require("./load");

const ui = load(["js/core.js", "js/graph-layout.js"]);

test("edges point from a layer to a lower one; cycles are drawn, not obeyed", () => {
  const nodes = ["app", "lib", "std", "a", "b"].map((id) => ({ id }));
  const edges = [{ from: "app", to: "lib" }, { from: "lib", to: "std" }, { from: "app", to: "std" }, { from: "a", to: "b" }, { from: "b", to: "a" }];
  const l = ui.graphLayout.layout(nodes, edges);
  const at = new Map(l.nodes.map((n) => [n.id, n]));
  for (const e of l.edges) if (!e.back) assert.ok(at.get(e.from).layer < at.get(e.to).layer, `${e.from} -> ${e.to}`);
  assert.equal(l.edges.filter((e) => e.back).length, 1);
  const byLayer = new Map();
  for (const n of l.nodes) {
    const row = byLayer.get(n.layer) || [];
    for (const m of row) assert.ok(Math.abs(m.x - n.x) >= n.width, "overlap");
    row.push(n);
    byLayer.set(n.layer, row);
  }
  assert.ok(l.width > 0 && l.height > 0);
});

test("a neighbourhood is the nodes within reach, both ways", () => {
  const nodes = ["a", "b", "c", "d"].map((id) => ({ id }));
  const edges = [{ from: "a", to: "b" }, { from: "b", to: "c" }, { from: "c", to: "d" }];
  const one = ui.graphLayout.neighbourhood(nodes, edges, "b", 1);
  assert.deepEqual(one.nodes.map((n) => n.id).sort(), ["a", "b", "c"]);
  const two = ui.graphLayout.neighbourhood(nodes, edges, "b", 2);
  assert.equal(two.nodes.length, 4);
});

test("strings and sizes", () => {
  ui.setBundle({ "{0} packages": "{0} 个包" });
  assert.equal(ui.t("{0} packages", 3), "3 个包");
  assert.equal(ui.t("missing {0}", "x"), "missing x");
  assert.equal(ui.formatBytes(1536), "1.5 KiB");
  assert.equal(ui.formatBytes(10), "10 B");
});
