// The graph page mcpp-clion shows in JCEF (mcpp design 2026-10-30 part 2 §9): the backend's
// `mcpp-ide/graph/packages` or `mcpp-ide/graph/modules`, drawn with mermaid. The page decides
// nothing: the IDE hands it the backend's answer and its strings (`mcppRender`), and a click on a
// module whose source is known asks the IDE to open it (`mcppHostSend`).

interface GraphNode {
  id: string;
  kind?: string;
  family?: string;
  units?: number;
  set?: string;
  source?: string;
}

interface GraphEdge {
  from: string;
  to: string;
}

export interface GraphPage {
  kind: "packages" | "modules";
  graph: { nodes: GraphNode[]; edges: GraphEdge[] };
  title: string;
  /** Said when the graph has no node. */
  empty: string;
  /** Said when the graph was cut to `limit` nodes; `{0}` is the limit. */
  truncated: string;
  dark: boolean;
  limit?: number;
}

declare const mermaid: {
  initialize(options: object): void;
  render(id: string, text: string): Promise<{ svg: string }>;
};

declare global {
  interface Window {
    mcppRender(page: GraphPage): Promise<void>;
    mcppHostSend?: (text: string) => void;
  }
}

/** A label as mermaid reads it inside `"…"`. */
function label(text: string): string {
  return text.replace(/"/g, "#quot;").replace(/</g, "#lt;").replace(/>/g, "#gt;");
}

/** The flowchart, and which mermaid id stands for which node. */
export function flowchart(page: GraphPage): { text: string; nodes: Map<string, GraphNode>; cut: boolean } {
  const limit = page.limit ?? 300;
  const shown = page.graph.nodes.slice(0, limit);
  const idOf = new Map<string, string>();
  const nodes = new Map<string, GraphNode>();
  const lines = ["flowchart LR"];
  shown.forEach((node, i) => {
    const id = `n${i}`;
    idOf.set(node.id, id);
    nodes.set(id, node);
    const detail = page.kind === "packages" && node.kind ? `<br/><small>${label(node.kind)}</small>` : "";
    lines.push(`  ${id}["${label(node.id)}${detail}"]`);
    if (node.kind === "external") lines.push(`  class ${id} external`);
    else if (node.kind === "test") lines.push(`  class ${id} test`);
  });
  for (const edge of page.graph.edges) {
    const from = idOf.get(edge.from), to = idOf.get(edge.to);
    if (from && to) lines.push(`  ${from} --> ${to}`);
  }
  lines.push("  classDef external stroke-dasharray: 4 3");
  lines.push("  classDef test stroke-width: 2px");
  return { text: lines.join("\n"), nodes, cut: page.graph.nodes.length > shown.length };
}

let renders = 0;

window.mcppRender = async (page: GraphPage): Promise<void> => {
  const title = document.getElementById("title")!;
  const note = document.getElementById("note")!;
  const target = document.getElementById("graph")!;
  title.textContent = page.title;
  document.body.classList.toggle("dark", page.dark);
  if (page.graph.nodes.length === 0) {
    note.textContent = page.empty;
    target.innerHTML = "";
    return;
  }
  const { text, nodes, cut } = flowchart(page);
  note.textContent = cut ? page.truncated.replace("{0}", String(page.limit ?? 300)) : "";
  mermaid.initialize({ startOnLoad: false, theme: page.dark ? "dark" : "default", securityLevel: "strict", maxEdges: 5000 });
  const { svg } = await mermaid.render(`graph${++renders}`, text);
  target.innerHTML = svg;
  // A module's source opens in the IDE.
  for (const element of Array.from(target.querySelectorAll<SVGGElement>("g.node"))) {
    const match = /-(n\d+)-/.exec(element.id);
    const node = match ? nodes.get(match[1]) : undefined;
    if (!node?.source) continue;
    element.style.cursor = "pointer";
    element.addEventListener("click", () => window.mcppHostSend?.(JSON.stringify({ open: node.source })));
  }
};
