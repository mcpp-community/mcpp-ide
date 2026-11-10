// The protocol's values in VS Code's terms.
import * as assert from "node:assert/strict";
import { test } from "node:test";

import { availableDebuggers, caseId, checksText, caseSummary, caseTags, fileId, format, mainLineOf, measured, metricText, parseCaseId, parseFileId, parseTestId, severityOf, testId, zeroBased } from "../../src/ide/mapping";

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

test("a case's checks read as the count that failed of those it ran", () => {
  assert.equal(checksText({ held: 3, failed: 2 }), "2 of 5 checks failed");
  assert.equal(caseSummary({ status: "failed", durationMs: 4, checks: { held: 3, failed: 2 } }), "✗ · 4 ms · 2 of 5 checks failed");
  assert.equal(caseSummary({ status: "passed", durationMs: 4, checks: { held: 3, failed: 0 } }), "✓ · 4 ms");
});

test("main is the language service's top-level function of that name", () => {
  const at = (line: number) => ({ start: { line } });
  assert.equal(mainLineOf([{ name: "helper", kind: 11, range: at(1) }, { name: "main", kind: 11, range: at(7) }]), 7);
  assert.equal(mainLineOf([{ name: "main(int, char **)", kind: 11, range: at(3) }]), 3);
  assert.equal(mainLineOf([{ name: "main", kind: 12, range: at(3) }]), undefined);
  assert.equal(mainLineOf(undefined), undefined);
});

test("a file of a program of several files is a part of its program (design 2026-11-10)", () => {
  const id = fileId("/w/p", "", "unit", "unit/a");
  assert.deepEqual(parseFileId(id), { root: "/w/p", member: "", test: "unit", part: "unit/a" });
  assert.equal(parseTestId(id), undefined);
  assert.equal(parseCaseId(id), undefined);
  assert.equal(parseFileId(testId("/w/p", "", "unit")), undefined);
  assert.equal(parseFileId(caseId("/w/p", "", "unit", "alpha")), undefined);
});

test("a case's line after a run: its verdict, duration and metrics against their bounds", () => {
  assert.equal(caseSummary({ status: "passed", durationMs: 3 }), "✓ · 3 ms");
  assert.equal(caseSummary({ status: "failed", durationMs: 12 }), "✗ · 12 ms");
  assert.equal(caseSummary({ status: "skipped", durationMs: 0 }), "○");
  assert.equal(caseSummary({ status: "passed", durationMs: 0, expectedFailure: true }), "✓ xfail · 0 ms");
  assert.equal(caseSummary({ status: "passed", durationMs: 4200, metrics: [{ name: "ready", value: 4.2, unit: "s" }],
                             bounds: [{ metric: "ready", max: 15 }] }), "✓ · 4200 ms · ready 4.2 s ≤ 15 s");
  assert.equal(metricText("recall", 0.9312, "1", { metric: "recall", min: 0.9 }), "recall 0.931 ≥ 0.9");
  assert.equal(metricText("peak", 1250.4, "ms", undefined), "peak 1250 ms");
  assert.equal(measured(0.000123, "s"), "0.000123 s");
});

test("a case's test tags: its kind and its own", () => {
  assert.deepEqual(caseTags("bench", ["slow"]), ["kind:bench", "slow"]);
  assert.deepEqual(caseTags(undefined, undefined), []);
});
