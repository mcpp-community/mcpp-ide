// The graph page as mcpp-clion shows it, in Chromium (the engine of JCEF): a real `mcpp-ide serve`
// answers `mcpp-ide/graph/packages` and `mcpp-ide/graph/modules` for the mock mcpp's build database,
// and the page draws what it is handed -- every node, the edges, an external module dashed -- says
// so when there is nothing to draw, and asks the IDE to open a module's source when it is clicked.
//
// Run from mide-plugins/vscode: npm run test:webui (it bundles the page first). MCPP_IDE_BACKEND and
// MCPP_IDE_MOCK_MCPP name the programs; else the newest of the repository's target/ are taken.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { createRequire } from "node:module";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, "..", "..");
const extension = path.join(repo, "mide-plugins", "vscode");
const require = createRequire(path.join(extension, "package.json"));
const { chromium } = require("playwright");
const rpc = require("vscode-jsonrpc/node");

function built(name) {
  const env = name === "mcpp-ide" ? process.env.MCPP_IDE_BACKEND : process.env.MCPP_IDE_MOCK_MCPP;
  if (env) return env;
  const exe = process.platform === "win32" ? `${name}.exe` : name;
  let best, bestTime = 0;
  const target = path.join(repo, "target");
  if (!fs.existsSync(target)) return undefined;
  for (const triple of fs.readdirSync(target, { withFileTypes: true }).filter((e) => e.isDirectory()).map((e) => e.name))
    for (const config of fs.readdirSync(path.join(target, triple), { withFileTypes: true })) {
      if (!config.isDirectory()) continue;
      const p = path.join(target, triple, config.name, "bin", exe);
      if (fs.existsSync(p) && fs.statSync(p).mtimeMs > bestTime) { best = p; bestTime = fs.statSync(p).mtimeMs; }
    }
  return best;
}

const page = path.join(extension, ".webui-test", "graph.html");

test("the graph page draws the backend's graphs", async (t) => {
  const backend = built("mcpp-ide"), mock = built("mcpp-ide-mock-mcpp");
  if (!backend || !mock) { t.skip("build the backend first (mcpp build)"); return; }
  assert.ok(fs.existsSync(page), "bundle the page first (npm run test:webui)");
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "mcpp-graph-"));
  fs.mkdirSync(path.join(root, "src"));
  fs.writeFileSync(path.join(root, "mcpp.toml"), '[package]\nname = "app"\nversion = "0.1.0"\n');

  const child = spawn(backend, ["serve"], { stdio: ["pipe", "pipe", "inherit"] });
  const connection = rpc.createMessageConnection(new rpc.StreamMessageReader(child.stdout), new rpc.StreamMessageWriter(child.stdin));
  connection.listen();
  const browser = await chromium.launch();
  try {
    await connection.sendRequest("initialize", {
      processId: process.pid,
      workspaceFolders: [{ uri: pathToFileURL(root).href, name: "app" }],
      capabilities: {},
      initializationOptions: { editor: "jetbrains", settings: { "mcpp.path": mock } },
    });
    const packages = await connection.sendRequest("mcpp-ide/graph/packages", { root });
    const modules = await connection.sendRequest("mcpp-ide/graph/modules", { root });
    assert.ok(packages.nodes.length > 0 && modules.nodes.length > 0, JSON.stringify({ packages, modules }).slice(0, 400));

    const tab = await browser.newPage();
    const opened = [];
    await tab.exposeFunction("mcppHostSend", (text) => opened.push(JSON.parse(text)));
    await tab.goto(pathToFileURL(page).href);
    const draw = (graph, kind) => tab.evaluate(([g, k]) => window.mcppRender({
      kind: k, graph: g, title: `${k} graph`, empty: "nothing to draw", truncated: "the first {0} nodes", dark: false,
    }), [graph, kind]);

    await draw(packages, "packages");
    const drawn = await tab.locator("#graph svg").innerHTML();
    for (const node of packages.nodes) assert.ok(drawn.includes(node.id), `${node.id} is not drawn`);
    assert.equal(await tab.locator("#title").textContent(), "packages graph");

    await draw(modules, "modules");
    assert.equal(await tab.locator("#graph g.node").count(), modules.nodes.length);
    assert.equal(await tab.locator("#graph g.node.external").count(), modules.nodes.filter((n) => n.kind === "external").length);
    const withSource = modules.nodes.findIndex((n) => n.source);
    if (withSource >= 0) {
      await tab.locator(`#graph g.node[id*="-n${withSource}-"]`).click();
      await tab.waitForTimeout(200);
      assert.deepEqual(opened, [{ open: modules.nodes[withSource].source }]);
    }

    await draw({ nodes: [], edges: [] }, "modules");
    assert.equal(await tab.locator("#note").textContent(), "nothing to draw");
    assert.equal(await tab.locator("#graph svg").count(), 0);

    // A large graph is cut, and says where.
    const many = { nodes: Array.from({ length: 5 }, (_, i) => ({ id: `m${i}` })), edges: [] };
    await tab.evaluate((g) => window.mcppRender({ kind: "modules", graph: g, title: "t", empty: "", truncated: "the first {0} nodes", dark: true, limit: 3 }), many);
    assert.equal(await tab.locator("#note").textContent(), "the first 3 nodes");
    assert.equal(await tab.locator("#graph g.node").count(), 3);
  } finally {
    await browser.close();
    const ended = new Promise((resolve) => child.on("exit", resolve));
    await connection.sendRequest("shutdown").catch(() => undefined);
    await connection.sendNotification("exit").catch(() => undefined);
    await ended;
    connection.dispose();
    fs.rmSync(root, { recursive: true, force: true });
  }
});
