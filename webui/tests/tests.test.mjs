// The tests page as mcpp-clion shows it in JCEF and mcpp-vscode in its Test Insights webview, in
// Chromium: a real `mcpp-ide serve` answers `mcpp-ide/test/insights` from the store mcpp keeps (the
// fixture's three runs, the last with mcpp#917's kinds and metrics), and the page draws what it is
// handed -- the run, its failure with both sides, the metric against its bound, the flaky case, the
// schedule with its critical path -- asks the IDE to open a failure's place when it is clicked, and
// says so when no run is kept.
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

const page = path.join(extension, ".webui-test", "tests.html");

const labels = {
  title: "Test Insights", noRun: "no run in {0}", window: "{0} runs · {1}", failures: "Failures", fresh: "new", since: "since {0}",
  expected: "expected", actual: "actual", metrics: "Metrics", slowest: "Slowest", median: "median {0}", flaky: "Flaky",
  flakyCounts: "{0}/{1}/{2}", schedule: "Schedule", worker: "w{0}", criticalPath: "Critical path", noMetrics: "no metrics",
};

test("the tests page draws what the store's runs say", async (t) => {
  const backend = built("mcpp-ide"), mock = built("mcpp-ide-mock-mcpp");
  if (!backend || !mock) { t.skip("build the backend first (mcpp build)"); return; }
  assert.ok(fs.existsSync(page), "bundle the page first (npm run test:webui)");
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "mcpp-tests-"));
  fs.mkdirSync(path.join(root, "tests", "unit"), { recursive: true });
  fs.writeFileSync(path.join(root, "mcpp.toml"), '[package]\nname = "app"\nversion = "0.1.0"\n\n[test]\nprograms = "directory"\n');
  fs.writeFileSync(path.join(root, ".mock-mcpp-tests"), "directory");
  for (const f of ["a.cpp", "b.cpp"]) fs.writeFileSync(path.join(root, "tests", "unit", f), "\n");
  fs.writeFileSync(path.join(root, "tests", "smoke.cpp"), "int main() {}\n");

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
    const empty = await connection.sendRequest("mcpp-ide/test/insights", { root });
    fs.cpSync(path.join(repo, "protocol", "fixtures", "mtest-store"), path.join(root, "target", "mtest"), { recursive: true });
    await connection.sendRequest("mcpp-ide/test/list", { root });
    const insights = await connection.sendRequest("mcpp-ide/test/insights", { root });
    assert.equal(insights.runs, 3, JSON.stringify(insights).slice(0, 400));

    const tab = await browser.newPage();
    const opened = [];
    await tab.exposeFunction("mcppHostSend", (text) => opened.push(JSON.parse(text)));
    await tab.goto(pathToFileURL(page).href);
    const draw = (x) => tab.evaluate(([i, l]) => window.mcppRender({ insights: i, labels: l, dark: false }), [x, labels]);

    await draw(insights);
    const text = await tab.locator("#tests").textContent();
    assert.match(text, /Test Insights/);
    assert.match(text, /3 runs/);
    assert.match(text, /beta/);
    assert.match(text, /expected: 4/);
    assert.match(text, /ready/);
    assert.match(text, /≤ 15 s/);
    assert.match(text, /Flaky/);
    assert.match(text, /Critical path: unit → smoke/);
    assert.equal(await tab.locator("#tests svg.spark").count(), 1);
    assert.equal(await tab.locator("#tests g.lane").count(), 2);
    assert.ok(await tab.locator("#tests rect.critical").count() >= 2);
    await tab.locator("#tests a[data-open]").first().click();
    await tab.waitForTimeout(200);
    assert.deepEqual(opened, [{ open: { path: "tests/unit/b.cpp", line: 3 } }]);

    // No run kept: the page says where mcpp keeps them.
    await draw(empty);
    assert.match(await tab.locator("#tests").textContent(), /no run in .*target\/mtest/);
    assert.equal(await tab.locator("#tests svg").count(), 0);
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
