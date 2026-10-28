// Every panel, mounted in a DOM against the real backend over the mock mcpp: the host is played the
// way the JetBrains plugin plays it (window.mcppHostSend out, window.mcppHostReceive in), and each
// panel must render what the golden fixtures hold without an error box.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { test, before, after } = require("node:test");

const repo = path.resolve(__dirname, "..", "..");
const vscodeDir = path.join(repo, "vscode");
const { JSDOM } = require(path.join(vscodeDir, "node_modules", "jsdom"));
const { Backend } = require(path.join(vscodeDir, "out", "src", "backend", "client.js"));
const { built } = require(path.join(vscodeDir, "out", "test", "unit", "harness.js"));

const scripts = ["core.js", "host.js", "graph-layout.js", "panels/project.js", "panels/libraries.js", "panels/cache.js",
  "panels/settings.js", "panels/graph.js", "panels/pack.js", "panels/targets.js", "app.js"].map((f) => path.join(repo, "webui", "js", f));

let backend, root, skip;
const hostCalls = [];

before(async () => {
  const program = built("mcpp-ide"), mock = built("mcpp-ide-mock-mcpp");
  if (!program || !mock) { skip = "build the backend first (mcpp build)"; return; }
  root = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-webui-"));
  fs.mkdirSync(path.join(root, "src"));
  fs.writeFileSync(path.join(root, "mcpp.toml"), '[package]\nname = "app"\nversion = "0.1.0"\n');
  backend = new Backend({ program });
  await backend.request("initialize", { editor: "jetbrains", settings: { "mcpp.path": mock }, workspaceFolders: [root] });
});

after(async () => { if (backend) await backend.stop(); });

function mount(panel, params) {
  const dom = new JSDOM('<!DOCTYPE html><main id="app"></main>', { runScripts: "outside-only", pretendToBeVisual: true });
  const w = dom.window;
  w.mcppHostSend = async (text) => {
    const m = JSON.parse(text);
    if (m.type === "ready") { w.mcppHostReceive({ type: "init", panel, params, bundle: {} }); return; }
    if (m.type === "rpc") {
      try { w.mcppHostReceive({ type: "rpcResult", id: m.id, result: await backend.request(m.method, m.params) }); }
      catch (e) { w.mcppHostReceive({ type: "rpcError", id: m.id, error: { message: e.message } }); }
      return;
    }
    if (m.type === "host") {
      hostCalls.push(m.action);
      const answers = { settings: {}, setSetting: true, confirm: { confirmed: false } };
      w.mcppHostReceive({ type: "hostResult", id: m.id, result: answers[m.action] ?? true });
    }
  };
  for (const f of scripts) w.eval(fs.readFileSync(f, "utf8"));
  return w;
}

async function settle(w, predicate, what) {
  for (let i = 0; i < 200; i++) {
    if (predicate(w.document)) return;
    await new Promise((r) => setTimeout(r, 25));
  }
  assert.fail(`${what}: ${w.document.getElementById("app").textContent.slice(0, 400)}`);
}

const noError = (w) => assert.equal(w.document.querySelector(".error"), null, w.document.querySelector(".error")?.textContent);

test("project", async (t) => {
  if (skip) return t.skip(skip);
  const w = mount("project", { root });
  await settle(w, (d) => d.body.textContent.includes("Tests (1)"), "the project's test");
  assert.ok(w.document.body.textContent.includes("app"));
  noError(w);
});

test("targets", async (t) => {
  if (skip) return t.skip(skip);
  const w = mount("targets", { root });
  await settle(w, (d) => d.querySelectorAll("table.targets tbody tr").length > 0, "target rows");
  assert.equal(w.document.querySelectorAll("tr.selected").length, 1);
  noError(w);
});

test("libraries", async (t) => {
  if (skip) return t.skip(skip);
  const w = mount("libraries", { root });
  await settle(w, (d) => d.querySelectorAll("li.package").length > 0, "packages");
  w.document.querySelector("li.package").dispatchEvent(new w.MouseEvent("click", { bubbles: true }));
  await settle(w, (d) => d.querySelector("select") && d.body.textContent.includes("Add to mcpp.toml"), "a package page");
  noError(w);
});

test("cache", async (t) => {
  if (skip) return t.skip(skip);
  const w = mount("cache", { root });
  await settle(w, (d) => d.querySelectorAll("table.bars tr").length > 0, "cache bars");
  noError(w);
});

test("settings", async (t) => {
  if (skip) return t.skip(skip);
  const w = mount("settings", {});
  await settle(w, (d) => d.body.textContent.includes("mcpp.path"), "the settings");
  noError(w);
});

test("graphs", async (t) => {
  if (skip) return t.skip(skip);
  for (const graph of ["packages", "modules"]) {
    const w = mount("graph", { root, graph });
    await settle(w, (d) => d.querySelectorAll(".node").length > 0, `${graph} nodes`);
    noError(w);
  }
});

test("pack", async (t) => {
  if (skip) return t.skip(skip);
  const w = mount("pack", { root });
  await settle(w, (d) => d.querySelectorAll("select option").length > 2, "pack formats");
  noError(w);
});
