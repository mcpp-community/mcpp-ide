// A layered layout for the dependency graphs: every edge points from a node to one it depends on,
// layers are assigned by longest path from the roots (cycles broken at the edge that closes them),
// and the order within a layer is improved by barycentre sweeps. Pure: nodes and edges in, positions
// out; the graph panel draws them.
(function (root) {
  "use strict";
  const ui = (root.McppUi = root.McppUi || /** @type {McppUiApi} */ ({}));

  // nodes: [{id}], edges: [{from, to}] -> {nodes: [{id, x, y, layer}], edges: [{from, to, points}], width, height}
  function layout(nodes, edges, opts = {}) {
    const nodeW = opts.nodeWidth || 180, nodeH = opts.nodeHeight || 28, gapX = opts.gapX || 24, gapY = opts.gapY || 64;
    const ids = nodes.map((n) => n.id);
    const index = new Map(ids.map((id, i) => [id, i]));
    const out = ids.map(() => []), inn = ids.map(() => []);
    const kept = [];
    for (const e of edges) {
      const a = index.get(e.from), b = index.get(e.to);
      if (a === undefined || b === undefined || a === b) continue;
      kept.push([a, b]);
    }
    // Break cycles: a DFS marks back edges, which are drawn but do not constrain the layers.
    const state = new Array(ids.length).fill(0);
    const back = new Set();
    const adj = ids.map(() => []);
    kept.forEach(([a, b], k) => adj[a].push([b, k]));
    for (let s = 0; s < ids.length; ++s) {
      if (state[s]) continue;
      const stack = [[s, 0]];
      state[s] = 1;
      while (stack.length) {
        const top = stack[stack.length - 1];
        const [v, i] = top;
        if (i < adj[v].length) {
          top[1]++;
          const [w, k] = adj[v][i];
          if (state[w] === 1) back.add(k);
          else if (state[w] === 0) { state[w] = 1; stack.push([w, 0]); }
        } else { state[v] = 2; stack.pop(); }
      }
    }
    kept.forEach(([a, b], k) => { if (!back.has(k)) { out[a].push(b); inn[b].push(a); } });
    // Longest path from the roots (nodes nothing depends on): a dependency sits below its users.
    const layer = new Array(ids.length).fill(0);
    const indeg = inn.map((l) => l.length);
    const queue = [];
    indeg.forEach((d, i) => { if (d === 0) queue.push(i); });
    for (let q = 0; q < queue.length; ++q) {
      const v = queue[q];
      for (const w of out[v]) {
        layer[w] = Math.max(layer[w], layer[v] + 1);
        if (--indeg[w] === 0) queue.push(w);
      }
    }
    const layers = [];
    layer.forEach((l, i) => { (layers[l] = layers[l] || []).push(i); });
    for (let l = 0; l < layers.length; ++l) if (!layers[l]) layers[l] = [];
    // Barycentre sweeps, down then up.
    const pos = new Array(ids.length).fill(0);
    const place = () => layers.forEach((row) => row.forEach((v, i) => { pos[v] = i; }));
    place();
    const sweep = (rows, neighbours) => {
      for (const row of rows) {
        const key = new Map(row.map((v) => {
          const ns = neighbours[v];
          return [v, ns.length ? ns.reduce((s, w) => s + pos[w], 0) / ns.length : pos[v]];
        }));
        row.sort((a, b) => key.get(a) - key.get(b) || a - b);
        row.forEach((v, i) => { pos[v] = i; });
      }
    };
    for (let k = 0; k < (opts.sweeps || 4); ++k) {
      sweep(layers.slice(1), inn);
      sweep(layers.slice(0, -1).reverse(), out);
    }
    const widest = Math.max(1, ...layers.map((r) => r.length));
    const width = widest * (nodeW + gapX) + gapX;
    const placed = nodes.map((n, i) => {
      const row = layers[layer[i]];
      const offset = (width - row.length * (nodeW + gapX) - gapX) / 2;
      return Object.assign({}, n, {
        layer: layer[i],
        x: offset + gapX + pos[i] * (nodeW + gapX),
        y: gapY / 2 + layer[i] * (nodeH + gapY),
        width: nodeW,
        height: nodeH,
      });
    });
    const routed = kept.map(([a, b], k) => {
      const p = placed[a], q = placed[b];
      return {
        from: ids[a], to: ids[b], back: back.has(k),
        points: [[p.x + nodeW / 2, p.y + nodeH], [q.x + nodeW / 2, q.y]],
      };
    });
    return { nodes: placed, edges: routed, width, height: layers.length * (nodeH + gapY) + gapY / 2 };
  }

  // The part of a graph around one node: it, what it depends on and what depends on it, `depth`
  // steps each way. Large module graphs are read this way rather than all at once.
  function neighbourhood(nodes, edges, focus, depth = 1) {
    const keep = new Set([focus]);
    let frontier = new Set([focus]);
    for (let d = 0; d < depth; ++d) {
      const next = new Set();
      for (const e of edges) {
        if (frontier.has(e.from) && !keep.has(e.to)) next.add(e.to);
        if (frontier.has(e.to) && !keep.has(e.from)) next.add(e.from);
      }
      next.forEach((v) => keep.add(v));
      frontier = next;
    }
    return {
      nodes: nodes.filter((n) => keep.has(n.id)),
      edges: edges.filter((e) => keep.has(e.from) && keep.has(e.to)),
    };
  }

  ui.graphLayout = { layout, neighbourhood };
})(typeof window !== "undefined" ? window : globalThis);
