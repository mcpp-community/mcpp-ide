/**
 * The graph page mcpp-clion shows in JCEF (webui/, mcpp design 2026-10-30 part 2 §9): webui/src/graph.ts
 * bundled into graph.js, beside graph.html and mermaid, into webui/dist (or the directory named by
 * --out), with files.txt listing them (what mcpp-clion extracts from its jar). Nothing it writes is
 * kept in the repository.
 */
import { build } from "esbuild";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const extension = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const webui = path.resolve(extension, "..", "..", "webui");
const at = process.argv.indexOf("--out");
const out = at > 0 ? path.resolve(process.argv[at + 1]) : path.join(webui, "dist");

fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(path.join(out, "vendor"), { recursive: true });
await build({
  entryPoints: [path.join(webui, "src", "graph.ts")],
  bundle: true,
  format: "iife",
  platform: "browser",
  target: "chrome120",
  outfile: path.join(out, "graph.js"),
  logLevel: "warning",
  legalComments: "eof",
});
fs.copyFileSync(path.join(webui, "graph.html"), path.join(out, "graph.html"));
const mermaid = path.join(extension, "node_modules", "@mermaid-js", "tiny");
fs.copyFileSync(path.join(mermaid, "dist", "mermaid.tiny.js"), path.join(out, "vendor", "mermaid.js"));
fs.copyFileSync(path.join(mermaid, "LICENSE"), path.join(out, "vendor", "MERMAID-LICENSE"));
const listed = [];
const list = (dir) => {
  for (const e of fs.readdirSync(dir, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) list(p);
    else listed.push(path.relative(out, p).split(path.sep).join("/"));
  }
};
list(out);
fs.writeFileSync(path.join(out, "files.txt"), `${listed.join("\n")}\n`);
console.log(`bundle-webui: ${path.relative(process.cwd(), out) || "."}`);
