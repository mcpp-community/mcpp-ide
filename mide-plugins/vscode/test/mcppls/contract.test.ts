import assert from "node:assert/strict";
import test from "node:test";

import { CLI_COMMANDS, DEPRECATED_COMMANDS } from "../../src/commands/ids";
import { CAPABILITIES, MCPPLS_EXTENSION_ID, capability, capabilityProblems } from "../../src/mcppls/contract";

test("the capability table satisfies its own rules", () => {
  assert.deepEqual(capabilityProblems(), []);
});

test("the dependency id is the published one", () => {
  assert.equal(MCPPLS_EXTENSION_ID, "sunrisepeak.mcpp-language-server");
});

test("the table keeps mcppls's action ids and no command id of either extension", () => {
  for (const entry of CAPABILITIES) {
    assert.ok(!entry.key.includes("."), `${entry.key} is a command id`);
    assert.ok(!("commands" in entry), `${entry.key} keeps a command of mcppls's`);
  }
});

test("destructive capabilities spell out what they do not touch", () => {
  const reset = capability("resetCache");
  assert.ok(reset);
  assert.equal(reset.danger, "destructive");
  assert.match(reset.confirmHint ?? "", /not touched/);
});

test("our own commands never use the mcppls prefix", () => {
  for (const id of [...Object.values(CLI_COMMANDS), ...Object.values(DEPRECATED_COMMANDS)]) {
    assert.ok(!id.startsWith("mcppls."), `${id} would collide with a server-advertised command`);
  }
});

test("capability keys are unique and unknown keys resolve to undefined", () => {
  const keys = CAPABILITIES.map((entry) => entry.key);
  assert.equal(new Set(keys).size, keys.length);
  assert.equal(capability("nope"), undefined);
});
