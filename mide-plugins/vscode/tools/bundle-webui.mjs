/**
 * The workbench mcpp-clion shows (webui/): mcpp-vscode's page modules, unchanged, with `vscode` and
 * Node's built-ins answered by webui/src, bundled into one script, beside the shell page, its
 * stylesheet, the codicons, and the files the pages load (media/, dist/vendor/), into
 * webui/dist (or the directory named by --out), with files.txt listing them (what mcpp-clion
 * extracts from its jar). Nothing it writes is kept in the repository.
 */
import { build } from "esbuild";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const extension = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const webui = path.resolve(extension, "..", "..", "webui");
const at = process.argv.indexOf("--out");
const out = at > 0 ? path.resolve(process.argv[at + 1]) : path.join(webui, "dist");
const shim = (name) => path.join(webui, "src", ...name.split("/"));

fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(out, { recursive: true });
await build({
  entryPoints: [shim("main.ts")],
  bundle: true,
  format: "iife",
  platform: "browser",
  target: "chrome120",
  outfile: path.join(out, "workbench.js"),
  alias: {
    vscode: shim("vscode.ts"),
    "node:fs": shim("node/fs.ts"),
    "node:path": shim("node/path.ts"),
    "node:os": shim("node/os.ts"),
    "node:crypto": shim("node/crypto.ts"),
    "node:url": shim("node/url.ts"),
    "node:util": shim("node/util.ts"),
    "node:child_process": shim("node/child_process.ts"),
  },
  inject: [shim("node/process.ts"), shim("node/buffer.ts")],
  nodePaths: [path.join(extension, "node_modules")],
  logLevel: "warning",
  legalComments: "eof",
});
for (const file of ["index.html", "workbench.css"]) fs.copyFileSync(path.join(webui, file), path.join(out, file));
const codicons = path.join(extension, "node_modules", "@vscode", "codicons", "dist");
for (const file of ["codicon.css", "codicon.ttf"]) fs.copyFileSync(path.join(codicons, file), path.join(out, file));
fs.cpSync(path.join(extension, "media"), path.join(out, "media"), { recursive: true });
fs.mkdirSync(path.join(out, "dist", "vendor"), { recursive: true });
fs.copyFileSync(path.join(extension, "node_modules", "@mermaid-js", "tiny", "dist", "mermaid.tiny.js"), path.join(out, "dist", "vendor", "mermaid.js"));
fs.copyFileSync(path.join(extension, "node_modules", "@mermaid-js", "tiny", "LICENSE"), path.join(out, "dist", "vendor", "MERMAID-LICENSE"));
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
