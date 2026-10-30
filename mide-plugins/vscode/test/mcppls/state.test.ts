import assert from "node:assert/strict";
import test from "node:test";

import type { LanguageServiceStatus } from "../../src/ide/protocol";
import { describeState, readStateFromStatus } from "../../src/mcppls/state";

function status(overrides: Partial<LanguageServiceStatus> = {}): LanguageServiceStatus {
  return {
    state: "ok",
    program: "/p/mcppls.okb",
    source: "editor",
    version: "0.0.20",
    required: "0.0.18",
    message: "mcppls 0.0.20",
    actions: [],
    statusSource: "status",
    capabilities: ["status"],
    serverActions: [],
    workspace: {
      root: "/w",
      state: "ready",
      engine: { name: "clangd", version: "23.1.0-mcppls.5", restarts: 1 },
      kit: { name: "mcppls-kit-x86_64-linux-gnu", state: "missing", install: "xlings install mcppls-kit-x86_64-linux-gnu", inUse: "openkal" },
      mcpp: { version: "2026.10.31.1", program: "/x/mcpp" },
      problems: [{ code: "kit-missing", message: "the kit is missing" }, { code: 3 }],
    },
    instances: [{ pid: 1 }, { pid: 2 }],
    ...overrides,
  };
}

test("reads what mcppls status states about the workspace", () => {
  const view = readStateFromStatus(status(), { enabled: true });
  assert.equal(view.available, true);
  assert.equal(view.state, "ready");
  assert.equal(view.version, "0.0.20");
  assert.equal(view.enabled, true);
  assert.deepEqual(view.engine, { name: "clangd", version: "23.1.0-mcppls.5", restarts: 1 });
  assert.equal(view.kit?.state, "missing");
  assert.equal(view.kit?.inUse, "openkal");
  assert.equal(view.mcpp?.version, "2026.10.31.1");
  assert.equal(view.instances, 2);
  assert.deepEqual(view.issues, [{ code: "kit-missing", message: "the kit is missing" }]);
});

test("each state of the workspace is kept, an unknown one is not", () => {
  for (const state of ["ready", "starting", "degraded", "stopped", "none"]) {
    assert.equal(readStateFromStatus(status({ workspace: { state } })).state, state);
  }
  const melted = readStateFromStatus(status({ workspace: { state: "melted" } }));
  assert.equal(melted.available, false);
  assert.match(melted.reason ?? "", /unknown state/);
});

test("an mcppls before status, a status that failed, or no mcppls says why there is no state", () => {
  const frozen = readStateFromStatus(status({ version: "0.0.19", statusSource: "frozen", workspace: null }));
  assert.equal(frozen.available, false);
  assert.match(frozen.reason ?? "", /0\.0\.19 has no status command/);
  const failed = readStateFromStatus(status({ statusSource: "frozen", statusError: "did not answer within 5 s", workspace: null }));
  assert.match(failed.reason ?? "", /did not answer/);
  const missing = readStateFromStatus(status({ state: "missing", message: "mcppls was not found" }));
  assert.equal(missing.reason, "mcppls was not found");
  assert.match(readStateFromStatus(undefined).reason ?? "", /not answered yet/);
});

test("one line for the self-check", () => {
  assert.match(describeState(readStateFromStatus(status())), /^ready · engine clangd 23\.1\.0-mcppls\.5 · kit mcppls-kit-x86_64-linux-gnu \(missing\) · 2 servers · 1 issue\(s\)$/);
  assert.match(describeState(readStateFromStatus(undefined)), /^unavailable \(/);
});
