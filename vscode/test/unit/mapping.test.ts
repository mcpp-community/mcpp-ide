// The protocol's values in VS Code's terms.
import * as assert from "node:assert/strict";
import { test } from "node:test";

import { availableDebuggers, format, parseTestId, severityOf, testId, zeroBased } from "../../src/mapping";

test("positions are 1-based in mcpp and 0-based in VS Code", () => {
  assert.deepEqual(zeroBased({ line: 1, column: 21 }), { line: 0, character: 20 });
  assert.deepEqual(zeroBased(undefined), { line: 0, character: 0 });
});

test("severities", () => {
  assert.equal(severityOf({ severity: "error" }), "error");
  assert.equal(severityOf({ severity: "note" }), "hint");
  assert.equal(severityOf({ severity: "info" }), "information");
});

test("a test's identity round-trips, roots with :: included", () => {
  const id = testId("/a::b", "m", "t");
  assert.deepEqual(parseTestId(id), { root: "/a::b", member: "m", test: "t" });
  assert.equal(parseTestId("x"), undefined);
});

test("the debuggers present, in order of preference", () => {
  assert.deepEqual(availableDebuggers((id) => id === "ms-vscode.cpptools" || id === "vadimcn.vscode-lldb"), ["lldb", "cppdbg"]);
  assert.equal(format("{0} and {1}", ["a", 2]), "a and 2");
});
