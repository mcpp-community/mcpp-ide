// The wire against the real backend, through the JSON-RPC library VS Code's language client is built
// on: `initialize` in LSP's shape over the mock mcpp finds the project; a build's notifications
// arrive under `mcpp-ide/` and end in job/finished; mcpp.toml opened has its diagnostics published,
// and completion and hover answer for it (design 2026-10-30 part 2 §8).
import * as assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { pathToFileURL } from "node:url";
import * as path from "node:path";
import { test } from "node:test";

import { createMessageConnection, StreamMessageReader, StreamMessageWriter, type MessageConnection } from "vscode-jsonrpc/node";

import { runProcess, useMcppExecutor } from "../../src/cli/process";
import { built, project } from "./harness";

interface Session {
  connection: MessageConnection;
  request<T>(method: string, params?: unknown): Promise<T>;
  stop(): Promise<void>;
}

/** `mcpp-ide serve`, initialized for a workspace folder over the mock mcpp. */
async function start(backend: string, mock: string, root: string): Promise<{ session: Session; init: any }> {
  const child = spawn(backend, ["serve"], { stdio: ["pipe", "pipe", "pipe"], windowsHide: true });
  const connection = createMessageConnection(new StreamMessageReader(child.stdout!), new StreamMessageWriter(child.stdin!));
  connection.listen();
  const ended = new Promise<void>((resolve) => child.on("exit", () => resolve()));
  const session: Session = {
    connection,
    request: <T>(method: string, params: unknown = {}) => connection.sendRequest(`mcpp-ide/${method}`, params) as Promise<T>,
    async stop() {
      await connection.sendRequest("shutdown");
      connection.sendNotification("exit");
      await ended;
      connection.dispose();
    },
  };
  const init = await connection.sendRequest("initialize", {
    processId: process.pid,
    clientInfo: { name: "test" },
    workspaceFolders: [{ uri: pathToFileURL(root).href, name: "app" }],
    capabilities: {},
    initializationOptions: { editor: "vscode", settings: { "mcpp.path": mock } },
  });
  connection.sendNotification("initialized", {});
  return { session, init };
}

test("a session over the mock mcpp", async (t) => {
  const backend = built("mcpp-ide"), mock = built("mcpp-ide-mock-mcpp");
  if (!backend || !mock) { t.skip("build the backend first (mcpp build)"); return; }
  const root = project();
  const { session, init } = await start(backend, mock, root);
  assert.equal(init.projects.length, 1);
  assert.equal(init.mcpp.contract, true);
  assert.equal(init.protocol, 2);
  assert.equal(init.capabilities.textDocumentSync.change, 1);
  const seen: string[] = [];
  for (const m of ["job/started", "job/plan", "job/progress", "job/artifact"])
    session.connection.onNotification(`mcpp-ide/${m}`, () => seen.push(m));
  const finished = new Promise((resolve) => session.connection.onNotification("mcpp-ide/job/finished", resolve));
  const { jobId } = await session.request<{ jobId: string }>("job/start", { kind: "build", root });
  assert.match(jobId, /^job-/);
  const done = (await finished) as { status: string };
  assert.equal(done.status, "ok");
  for (const m of ["job/started", "job/plan", "job/progress", "job/artifact"]) assert.ok(seen.includes(m), m);
  await assert.rejects(session.request("no/such/method"), /unknown method/);
  await session.stop();
});

test("mcpp.toml through LSP: diagnostics published, completion, hover", async (t) => {
  const backend = built("mcpp-ide"), mock = built("mcpp-ide-mock-mcpp");
  if (!backend || !mock) { t.skip("build the backend first (mcpp build)"); return; }
  const root = project();
  const { session } = await start(backend, mock, root);
  const uri = pathToFileURL(path.join(root, "mcpp.toml")).href;
  const published = new Promise<any>((resolve) => session.connection.onNotification("textDocument/publishDiagnostics", resolve));
  session.connection.sendNotification("textDocument/didOpen", {
    textDocument: { uri, languageId: "mcpp-toml", version: 1, text: "[package]\nname = \"名字\"\ncolour = 1\n[build]\nbmi_schedule = \"\"\n" },
  });
  const diagnostics = await published;
  assert.equal(diagnostics.uri, uri);
  assert.deepEqual(diagnostics.diagnostics.map((d: any) => [d.code, d.range.start.line, d.range.start.character]),
    [["mcpp.manifest.unknownKey", 2, 0]]);
  const list = await session.connection.sendRequest<any>("textDocument/completion", { textDocument: { uri }, position: { line: 4, character: 16 } });
  assert.deepEqual(list.items.map((i: any) => i.label).sort(), ["auto", "off", "on"]);
  const hover = await session.connection.sendRequest<any>("textDocument/hover", { textDocument: { uri }, position: { line: 4, character: 2 } });
  assert.match(hover.contents.value, /bmi_schedule/);
  await session.stop();
});

test("mcpp runs through the backend, and the extension's parsers read what it printed", async (t) => {
  const backend = built("mcpp-ide"), mock = built("mcpp-ide-mock-mcpp");
  if (!backend || !mock) { t.skip("build the backend first (mcpp build)"); return; }
  const root = project();
  const { session } = await start(backend, mock, root);
  // 0.9.0's seam (`runProcess`) handed to the backend, as the extension does at activation.
  useMcppExecutor(async (_exe, args, cwd, options = {}) => {
    const r = await session.request<{ exitCode: number; stdout: string; stderr: string }>("mcpp/exec", {
      args, ...(cwd ? { cwd } : {}), ...(options.timeoutMs ? { timeoutMs: options.timeoutMs } : {}),
    });
    return { exitCode: r.exitCode, stdout: r.stdout, stderr: r.stderr };
  });
  try {
    const version = await runProcess("mcpp", ["--version"], root);
    assert.equal(version.exitCode, 0);
    assert.match(version.stdout, /^mcpp \d/);
    const files = await session.request<{ manifest: string; lock: string | null; sourceFiles: number }>("project/files", { root });
    assert.match(files.manifest, /^\[package\]/);
    assert.equal(files.lock, null);
    assert.equal(files.sourceFiles, 0);
  } finally {
    useMcppExecutor(undefined);
    await session.stop();
  }
});
