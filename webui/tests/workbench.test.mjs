// The workbench as mcpp-clion shows it, in Chromium (the engine of JCEF), with a host that answers
// the bridge as Workbench.kt does -- the backend's methods over a real `mcpp-ide serve` answering for
// the mock mcpp, files from disk, settings in memory, dialogs recorded -- and the synchronous channel
// as the JS dialog handler answers it. mcpp-vscode's pages must run in it unchanged: the views as
// tabs, the project tree, the cache page over `mcpp cache list`, the libraries over an index and a
// package's page, the settings page writing a setting, and the graph page.
//
// Run from mide-plugins/vscode: npm run test:webui (it bundles the workbench and compiles the
// extension first). MCPP_IDE_BACKEND and MCPP_IDE_MOCK_MCPP name the programs; else the newest of
// the repository's target/ are taken.
import assert from "node:assert/strict";
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
const { Backend } = require(path.join(extension, "dist", "src", "ide", "backend", "client.js"));

function built(name) {
  const env = name === "mcpp-ide" ? process.env.MCPP_IDE_BACKEND : process.env.MCPP_IDE_MOCK_MCPP;
  if (env) return env;
  const exe = process.platform === "win32" ? `${name}.exe` : name;
  let best, bestTime = 0;
  const walk = (dir, depth) => {
    if (depth > 4) return;
    let entries = [];
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const e of entries) {
      const p = path.join(dir, e.name);
      if (e.isDirectory()) walk(p, depth + 1);
      else if (e.name === exe && path.basename(dir) === "bin") { const t = fs.statSync(p).mtimeMs; if (t > bestTime) { best = p; bestTime = t; } }
    }
  };
  walk(path.join(repo, "target"), 0);
  return best;
}

/** A project, a home with one index descriptor, and a target/ with artifacts to measure. */
function fixture() {
  const base = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), "mcpp-workbench-")));
  const root = path.join(base, "app");
  fs.mkdirSync(path.join(root, "src"), { recursive: true });
  fs.writeFileSync(path.join(root, "mcpp.toml"), '[package]\nname = "app"\nversion = "0.1.0"\n\n[dependencies.compat]\nargparse = "3.2"\n');
  fs.writeFileSync(path.join(root, "src", "main.cpp"), "int main() {}\n");
  fs.mkdirSync(path.join(root, "target", "x86_64-linux-gnu", "obj"), { recursive: true });
  fs.writeFileSync(path.join(root, "target", "x86_64-linux-gnu", "obj", "main.o"), Buffer.alloc(4096));
  const home = path.join(base, "home");
  fs.mkdirSync(path.join(home, "indices", "mcpplibs", "pkgs", "c"), { recursive: true });
  fs.writeFileSync(path.join(home, "indices", "mcpplibs", "pkgs", "c", "compat.argparse.lua"),
    'package = { name = "argparse", namespace = "compat", description = "Argument parser", xpm = { linux = { ["3.2"] = {} }, macosx = { ["3.2"] = {} }, windows = { ["3.2"] = {} } } }');
  return { base, root, home };
}

function statJson(p, follow) {
  try {
    const s = follow ? fs.statSync(p) : fs.lstatSync(p);
    return { type: s.isSymbolicLink() ? "symlink" : s.isDirectory() ? "directory" : s.isFile() ? "file" : "other", size: s.size, mtimeMs: s.mtimeMs, ino: Number(s.ino % 4294967296) };
  } catch { return null; }
}

/** What Workbench.kt answers, for the bridge's actions. */
function host({ backend, root, home, dist, recorded }) {
  const settings = { user: { "mcpp.library.indexPath": "" }, workspace: {} };
  const files = (action, p) => {
    switch (action) {
      case "fs.stat": return statJson(p.path, p.follow !== false);
      case "fs.read": try { return fs.readFileSync(p.path, "utf8"); } catch { return null; }
      case "fs.list": try { return fs.readdirSync(p.path).map((name) => ({ name, ...statJson(path.join(p.path, name), false) })); } catch { return null; }
      case "fs.realpath": try { return fs.realpathSync(p.path); } catch { return null; }
      case "fs.write": fs.mkdirSync(path.dirname(p.path), { recursive: true }); fs.writeFileSync(p.path, p.base64 ? Buffer.from(p.text, "base64") : p.text); return true;
    }
    throw new Error(`unknown file action ${action}`);
  };
  return {
    files,
    async handle(action, p) {
      recorded.push({ action, params: p });
      switch (action) {
        case "init":
          return {
            info: {
              platform: process.platform, arch: process.arch, language: "en", home: os.homedir(), tmp: os.tmpdir(),
              env: { MCPP_HOME: home }, extensionUri: `${pathToFileURL(dist).href}/`, workspaceFolders: [root], trusted: true,
            },
            theme: { "--vscode-foreground": "#333", "--vscode-editor-background": "#fff", "--vscode-font-family": "sans-serif", "--vscode-font-size": "13px" },
            themeKind: "vscode-light",
            mode: "window",
          };
        case "rpc": return backend.request(p.method, p.params);
        case "settings.read": return settings;
        case "settings.write": settings[p.level === "user" ? "user" : "workspace"][p.key] = p.value; if (p.value === null) delete settings[p.level === "user" ? "user" : "workspace"][p.key]; return settings;
        case "progress.start": return 1;
        case "message": case "input": case "pick": case "saveDialog": return null;
        case "findFiles": return [];
        default:
          if (action.startsWith("fs.")) return files(action, p);
          return null;
      }
    },
  };
}

test("mcpp-vscode's pages run in the workbench, as mcpp-clion shows them", { timeout: 180_000 }, async (t) => {
  const program = built("mcpp-ide"), mock = built("mcpp-ide-mock-mcpp");
  if (!program || !mock) { t.skip("build the backend first (mcpp build)"); return; }
  const dist = process.env.MCPP_WEBUI_DIST ?? path.join(extension, ".webui-test");
  assert.ok(fs.existsSync(path.join(dist, "workbench.js")), "bundle the workbench first (npm run bundle:webui -- --out .webui-test)");
  const { base, root, home } = fixture();
  const backend = new Backend({ program, env: { ...process.env, HOME: home } });
  await backend.request("initialize", { editor: "jetbrains", settings: { "mcpp.path": mock }, workspaceFolders: [root], trusted: true });
  const recorded = [];
  const h = host({ backend, root, home, dist, recorded });
  const browser = await chromium.launch();
  const page = await browser.newPage();
  const errors = [];
  const consoleLines = [];
  page.on("pageerror", (e) => errors.push(String(e)));
  page.on("console", (m) => consoleLines.push(`${m.type()}: ${m.text()}`));
  page.on("dialog", async (dialog) => {
    const message = dialog.message();
    if (dialog.type() !== "prompt" || !message.startsWith("mcpp-host:")) { await dialog.dismiss(); return; }
    const { action, params } = JSON.parse(message.slice("mcpp-host:".length));
    let answer;
    try { answer = { ok: true, result: h.files(action, params) }; } catch (e) { answer = { ok: false, error: String(e) }; }
    await dialog.accept(JSON.stringify(answer));
  });
  await page.exposeBinding("mcppHostSend", async (_source, text) => {
    const m = JSON.parse(text);
    let reply;
    try { reply = { type: "result", id: m.id, result: (await h.handle(m.action, m.params)) ?? null }; }
    catch (e) { reply = { type: "error", id: m.id, error: { message: e instanceof Error ? e.message : String(e) } }; }
    await page.evaluate((r) => window.mcppHostReceive(r), reply).catch(() => undefined);
  });
  const notify = (method, params) => page.evaluate(([m, p]) => window.mcppHostReceive({ type: "notify", method: m, params: p }), [method, params]);
  try {
    await page.goto(pathToFileURL(path.join(dist, "index.html")).href);
    // The views, in the manifest's order and names.
    await page.waitForSelector(".tabs .tab");
    assert.deepEqual(await page.$$eval(".tabs .tab", (tabs) => tabs.map((t) => t.textContent.trim())), ["Project", "Libraries", "Cache"]);

    // The project tree: 0.9.0's rows for this project.
    await page.waitForSelector(".tree .row");
    const rows = await page.$$eval(".tree .row .label", (labels) => labels.map((l) => l.textContent));
    assert.ok(rows.length > 0, "the project tree has rows");

    // The cache page, over `mcpp cache list` through the backend.
    await page.click(".tabs .tab:nth-child(3)");
    const cacheFrame = await page.waitForSelector(".stage > section:not([hidden]) iframe.webview");
    await page.waitForFunction((f) => f.contentDocument?.body?.textContent?.includes("Global build cache"), cacheFrame, { timeout: 30_000 });
    const cacheText = await cacheFrame.evaluate((f) => f.contentDocument.body.textContent);
    assert.match(cacheText, /Project cache/);

    // The libraries page, over the index under MCPP_HOME.
    await page.click(".tabs .tab:nth-child(2)");
    const libraryFrame = await page.waitForSelector(".stage > section:not([hidden]) iframe.webview");
    await page.waitForFunction((f) => f.contentDocument?.querySelector("[data-row]")?.textContent?.includes("argparse"), libraryFrame, { timeout: 30_000 });

    // A package's page: the row opens it as a panel, a tab of its own.
    await libraryFrame.evaluate((f) => f.contentDocument.querySelector("[data-row]").click());
    await page.waitForFunction(() => document.querySelectorAll(".tabs .tab .close").length === 1, null, { timeout: 15_000 });
    const detailFrame = await page.waitForSelector(".stage > section:not([hidden]) iframe.webview");
    await page.waitForFunction((f) => f.contentDocument?.body?.textContent?.includes("Argument parser"), detailFrame, { timeout: 30_000 });

    // The settings page; a change it makes reaches the IDE's store.
    await notify("show", { view: "settings" });
    await page.waitForFunction(() => [...document.querySelectorAll(".tabs .tab")].some((t) => t.textContent.includes("settings")), null, { timeout: 15_000 });
    const settingsFrame = await page.waitForSelector(".stage > section:not([hidden]) iframe.webview");
    await page.waitForFunction((f) => f.contentDocument?.querySelector("input[type=checkbox][data-key]"), settingsFrame, { timeout: 30_000 });
    const writesBefore = recorded.filter((r) => r.action === "settings.write").length;
    await settingsFrame.evaluate((f) => f.contentDocument.querySelector("input[type=checkbox][data-key]").click());
    const deadline = Date.now() + 10_000;
    while (recorded.filter((r) => r.action === "settings.write").length === writesBefore && Date.now() < deadline) await new Promise((r) => setTimeout(r, 100));
    assert.ok(recorded.filter((r) => r.action === "settings.write").length > writesBefore, "the settings page wrote a setting");

    // The dependency graph page opens and names the project.
    await notify("show", { view: "graph:packages" });
    const graphFrame = await page.waitForSelector(".stage > section:not([hidden]) iframe.webview");
    await page.waitForFunction((f) => f.contentDocument?.querySelector("#project-name")?.textContent === "app", graphFrame, { timeout: 30_000 });

    assert.deepEqual(errors, [], "the workbench raised no error");
    assert.deepEqual(consoleLines.filter((l) => l.startsWith("error:")), [], "no page logged an error (a CSP refusal among them)");
  } catch (error) {
    const body = await page.evaluate(() => document.body.innerText).catch(() => "");
    const logs = recorded.filter((r) => r.action === "log").map((r) => r.params.text);
    console.error(`-- page --\n${body}\n-- errors --\n${errors.join("\n")}\n-- console --\n${consoleLines.join("\n")}\n-- log --\n${logs.join("")}`);
    throw error;
  } finally {
    await browser.close();
    await backend.stop();
    fs.rmSync(base, { recursive: true, force: true });
  }
});
