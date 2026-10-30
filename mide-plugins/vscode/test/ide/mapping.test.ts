// The protocol's values in VS Code's terms.
import * as assert from "node:assert/strict";
import { test } from "node:test";

import { availableDebuggers, baseCaseName, caseId, format, mainLineOf, parseCaseId, parseTestId, severityOf, testId, zeroBased } from "../../src/ide/mapping";

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

test("a case's id holds its program's and its name, which may hold ::", () => {
  const id = caseId("/w/p", "", "unit/math", "ns::Suite.Name");
  assert.deepEqual(parseCaseId(id), { root: "/w/p", member: "", test: "unit/math", name: "ns::Suite.Name" });
  assert.equal(parseTestId(id), undefined);
  assert.equal(parseCaseId(testId("/w/p", "", "unit/math")), undefined);
});

test("a GoogleTest result is matched to its source's case without instance and parameter", () => {
  assert.equal(baseCaseName("gtest", "Inst/Param.Works/1"), "Param.Works");
  assert.equal(baseCaseName("gtest", "Typed/0.Holds"), "Typed.Holds");
  assert.equal(baseCaseName("gtest", "Math.Adds"), "Math.Adds");
  assert.equal(baseCaseName("catch2", "a/b.c"), "a/b.c");
});

test("main is the language service's top-level function of that name", () => {
  const at = (line: number) => ({ start: { line } });
  assert.equal(mainLineOf([{ name: "helper", kind: 11, range: at(1) }, { name: "main", kind: 11, range: at(7) }]), 7);
  assert.equal(mainLineOf([{ name: "main(int, char **)", kind: 11, range: at(3) }]), 3);
  assert.equal(mainLineOf([{ name: "main", kind: 12, range: at(3) }]), undefined);
  assert.equal(mainLineOf(undefined), undefined);
});
