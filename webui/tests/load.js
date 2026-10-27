// Loads the panels' classic scripts into a context: Node's own global for the pure ones, a jsdom
// window for the ones that render.
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const webui = path.resolve(__dirname, "..");

function load(files, context) {
  const g = context || { console, setTimeout, clearTimeout };
  if (!context) { g.globalThis = g; vm.createContext(g); }
  for (const f of files) vm.runInContext(fs.readFileSync(path.join(webui, f), "utf8"), g, { filename: f });
  return g.McppUi;
}

module.exports = { load, webui };
