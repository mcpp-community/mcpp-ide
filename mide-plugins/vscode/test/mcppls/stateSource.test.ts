import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import test from "node:test";

/**
 * The C++ Modules state read, asserted on the source itself.
 *
 * `src/mcppls/stateSource.ts` is the `vscode` boundary, so its two rules are checked where they are
 * written: the state is the backend's `mcppls status` (design 2026-10-30 part 2 §5.2), never the
 * mcppls extension's API object; and `mcpp.languageService.readState` switches it off before
 * anything is read. Structure, not wording.
 */

const SOURCE = path.join(process.cwd(), "src", "mcppls", "stateSource.ts");

/** The text without comments, so a doc comment cannot satisfy or break the scan. */
function code(): string {
  return readFileSync(SOURCE, "utf8")
    .replace(/\/\*[\s\S]*?\*\//g, "")
    .replace(/\/\/[^\n]*/g, "");
}

test("the mcppls extension's API object is not read", () => {
  assert.ok(!/\.exports\b/.test(code()), "stateSource.ts reads extension.exports");
});

test("mcpp.languageService.readState is read, and its gate precedes the state", () => {
  const text = code();
  const gate = text.indexOf('read<boolean>("mcpp.languageService.readState")');
  assert.ok(gate >= 0, "stateSource.ts must read mcpp.languageService.readState to switch the state off");
  const use = text.indexOf("readStateFromStatus(status");
  assert.ok(use > gate, "the state must be read after the readState gate");
  const after = text.slice(gate, gate + 400);
  assert.match(after, /available:\s*false/);
  assert.match(after, /reason:/);
});

test("readLanguageServerState never throws", () => {
  const boundary = code().slice(code().indexOf("export function readLanguageServerState"));
  assert.ok(!/\bthrow\b/.test(boundary), "readLanguageServerState must never throw");
});
