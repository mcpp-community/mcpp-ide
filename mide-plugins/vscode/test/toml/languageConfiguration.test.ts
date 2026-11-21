/**
 * mcpp.toml's language configuration (#15, #18): brackets and quotes close as they are typed, `#`
 * comments a line, and a quote does not pair inside a string or a comment. The file is the one the
 * manifest names and the package carries.
 */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import test from "node:test";

const root = process.cwd();
const manifest = JSON.parse(readFileSync(path.join(root, "package.json"), "utf8"));
const language = manifest.contributes.languages.find((l: { id: string }) => l.id === "mcpp-toml");
const file: string = language?.configuration ?? "";
const configuration = JSON.parse(readFileSync(path.join(root, file), "utf8"));

test("mcpp-toml names its language configuration, which the package carries", () => {
  assert.ok(file.startsWith("./syntaxes/"), file);
  const ignored = readFileSync(path.join(root, ".vscodeignore"), "utf8").split(/\r?\n/);
  assert.ok(!ignored.some((line) => line.trim() === "syntaxes/**"), "syntaxes/ is packaged");
});

test("brackets and quotes close, a quote not inside a string or a comment", () => {
  const pairs = new Map<string, { close: string; notIn?: string[] }>(
    configuration.autoClosingPairs.map((p: { open: string; close: string; notIn?: string[] }) => [p.open, p]));
  assert.equal(pairs.get("[")?.close, "]");
  assert.equal(pairs.get("{")?.close, "}");
  for (const quote of ["\"", "'"]) {
    assert.equal(pairs.get(quote)?.close, quote);
    assert.deepEqual(pairs.get(quote)?.notIn, ["string", "comment"]);
  }
  assert.deepEqual(configuration.brackets, [["[", "]"], ["{", "}"]]);
});

test("a line comment is TOML's", () => {
  assert.equal(configuration.comments.lineComment, "#");
  assert.equal(configuration.comments.blockComment, undefined);
});
