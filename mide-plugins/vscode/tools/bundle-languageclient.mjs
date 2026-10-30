// vscode-languageclient, bundled into dist/vendor/languageclient.js: the extension connects to the
// mcpp-ide backend as to an LSP server (design 2026-10-30 part 2 §8), and the VSIX carries no
// node_modules. `vscode` stays the host's.
import { copyFileSync } from "node:fs";
import { build } from "esbuild";

await build({
  entryPoints: ["node_modules/vscode-languageclient/lib/node/main.js"],
  bundle: true,
  platform: "node",
  format: "cjs",
  target: "node18",
  external: ["vscode"],
  outfile: "dist/vendor/languageclient.js",
  logLevel: "warning",
  minify: true,
  keepNames: true,
  legalComments: "inline",
});
copyFileSync("node_modules/vscode-languageclient/License.txt", "dist/vendor/LANGUAGECLIENT-LICENSE");
