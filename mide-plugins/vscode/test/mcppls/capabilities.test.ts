import assert from "node:assert/strict";
import test from "node:test";

import type { ServerAction } from "../../src/ide/protocol";
import { MCPPLS_EXTENSION_ID } from "../../src/mcppls/contract";
import { CapabilityRegistry, classifyCommandError, type CapabilityEnvironment } from "../../src/mcppls/capabilities";

interface Harness {
  registry: CapabilityRegistry;
  calls: Array<{ command: string; args: unknown[] }>;
  activations: string[];
  answer(actions: readonly ServerAction[] | undefined): void;
}

/** mcppls's actions as `mcppls status` names them, a few of them. */
const ACTIONS: readonly ServerAction[] = [
  { id: "restart", title: "Restart", vscode: "mcppls.restartServer", jetbrains: "community.mcpp.mcppls.RestartServer" },
  { id: "moduleGraph", title: "Graph", vscode: "mcppls.showModuleGraph" },
  { id: "openLogDirectory", title: "Logs", vscode: "mcppls.revealCacheDirectory", vscodeArguments: ["logs"] },
  { id: "showStatus", title: "Status", nvim: "McpplsStatus" },
  { id: "exportDiagnostics", title: "Bundle", vscode: "mcppls.exportDiagnosticBundle" },
  { id: "somethingNew", title: "New", vscode: "mcppls.somethingNew" },
];

function harness(options: {
  installed?: boolean;
  actions?: readonly ServerAction[] | undefined;
  behaviour?: (command: string, attempt: number) => void;
  /** What the command answers with, for the few that answer at all. */
  result?: (command: string) => unknown;
} = {}): Harness {
  const installed = options.installed ?? true;
  let actions = "actions" in options ? options.actions : ACTIONS;
  const calls: Array<{ command: string; args: unknown[] }> = [];
  const activations: string[] = [];
  const attempts = new Map<string, number>();
  const environment: CapabilityEnvironment = {
    extensionInstalled: (id) => installed && id === MCPPLS_EXTENSION_ID,
    serverActions: () => actions,
    activateExtension: async (id) => {
      activations.push(id);
    },
    executeCommand: async <T>(command: string, ...args: unknown[]): Promise<T> => {
      calls.push({ command, args });
      const attempt = (attempts.get(command) ?? 0) + 1;
      attempts.set(command, attempt);
      options.behaviour?.(command, attempt);
      return (options.result === undefined ? undefined : options.result(command)) as T;
    },
  };
  const registry = new CapabilityRegistry(environment);
  return {
    registry,
    calls,
    activations,
    answer: (next) => {
      actions = next;
      registry.invalidate();
    },
  };
}

const notFound = (command: string): never => {
  throw new Error(`command '${command}' not found`);
};

test("classifyCommandError separates a missing command from a real failure", () => {
  assert.equal(classifyCommandError(new Error("command 'mcppls.showLogs' not found")), "missing");
  assert.equal(classifyCommandError(new Error("Command not found")), "missing");
  assert.equal(classifyCommandError(new Error("not registered")), "missing");
  assert.equal(classifyCommandError(new Error("server is not running")), "failed");
  assert.equal(classifyCommandError("plain string"), "failed");
});

test("an action runs the VS Code command mcppls names for it", async () => {
  const { registry, calls } = harness();
  assert.equal(registry.status("moduleGraph").state, "declared");
  const result = await registry.invoke("moduleGraph");
  assert.deepEqual(result, { state: "completed", capabilityKey: "moduleGraph", command: "mcppls.showModuleGraph" });
  assert.deepEqual(calls, [{ command: "mcppls.showModuleGraph", args: [] }]);
});

test("the arguments mcppls names are passed, unless the caller passes its own", async () => {
  const { registry, calls } = harness();
  await registry.invoke("openLogDirectory");
  await registry.invoke("openLogDirectory", "elsewhere");
  assert.deepEqual(calls, [
    { command: "mcppls.revealCacheDirectory", args: ["logs"] },
    { command: "mcppls.revealCacheDirectory", args: ["elsewhere"] },
  ]);
});

test("an action mcppls offers no VS Code command for, or does not name, is hidden and never called", async () => {
  const { registry, calls } = harness();
  assert.equal(registry.isGone("selectContext"), true);
  assert.equal((await registry.invoke("selectContext")).state, "missing");
  assert.equal(registry.isGone("showStatus"), true);
  assert.deepEqual(calls, []);
});

test("an action mcpp-ide has no policy for is not offered", () => {
  const { registry } = harness();
  assert.ok(!registry.availableKeys().includes("somethingNew"));
});

test("before the backend answers, an action is greyed out, not hidden", async () => {
  const { registry, calls, answer } = harness({ actions: undefined });
  assert.equal(registry.isUnconfirmed("restart"), true);
  assert.equal(registry.isGone("restart"), false);
  assert.equal((await registry.invoke("restart")).state, "missing");
  answer(ACTIONS);
  assert.equal(registry.isUnconfirmed("restart"), false);
  assert.equal((await registry.invoke("restart")).state, "completed");
  assert.deepEqual(calls, [{ command: "mcppls.restartServer", args: [] }]);
});

test("the command's own answer is passed back untouched", async () => {
  const { registry } = harness({ result: () => "/cache/bundles/b.zip" });
  const result = await registry.invoke("exportDiagnostics");
  assert.equal(result.value, "/cache/bundles/b.zip");
});

test("an uninstalled dependency makes every capability unavailable without calling anything", async () => {
  const { registry, calls } = harness({ installed: false });
  assert.equal(registry.isGone("restart"), true);
  assert.deepEqual(await registry.invoke("restart"), { state: "unavailable", capabilityKey: "restart" });
  assert.deepEqual(calls, []);
});

test("a command VS Code does not know is remembered as gone", async () => {
  const { registry, calls } = harness({ behaviour: notFound });
  assert.equal((await registry.invoke("restart")).state, "missing");
  assert.equal(registry.isGone("restart"), true);
  assert.equal((await registry.invoke("restart")).state, "missing");
  assert.equal(calls.length, 1);
});

test("a real failure keeps the capability and reports the error verbatim", async () => {
  const { registry } = harness({
    behaviour: () => {
      throw new Error("server is not running");
    },
  });
  const result = await registry.invoke("restart");
  assert.equal(result.state, "failed");
  assert.equal(result.error, "server is not running");
  assert.equal(registry.isGone("restart"), false);
});

test("the dependency is activated before the first forward", async () => {
  const { registry, activations } = harness();
  await registry.invoke("restart");
  assert.deepEqual(activations, [MCPPLS_EXTENSION_ID]);
});

test("an unknown capability fails without touching the dependency", async () => {
  const { registry, calls, activations } = harness();
  const result = await registry.invoke("nope");
  assert.equal(result.state, "failed");
  assert.deepEqual(calls, []);
  assert.deepEqual(activations, []);
});
