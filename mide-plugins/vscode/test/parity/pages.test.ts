/**
 * Parity gate (b): every page this extension draws is the page mcpp-vscode 0.9.0 drew for the same
 * view model. `pages.js` renders fixed models with a compiled tree's generators; the snapshots under
 * `snapshots/0.9.0/` are 0.9.0's own output for them (rendered from the v0.9.0 tag), and this
 * build must reproduce them byte for byte. A deliberate change to a page replaces its snapshot in
 * the same commit, with the reason in the commit message.
 */
import assert from "node:assert/strict";
import { readFileSync, readdirSync } from "node:fs";
import path from "node:path";
import test from "node:test";

const root = process.cwd();
// eslint-disable-next-line @typescript-eslint/no-require-imports
const { pages } = require(path.join(root, "test/parity/pages.js")) as { pages: (dist: string) => { name: string; html: string }[] };

test("each page renders as 0.9.0 rendered it", () => {
  const snapshots = path.join(root, "test/parity/snapshots/0.9.0");
  const rendered = pages(path.join(root, "dist"));
  assert.deepEqual(rendered.map((p) => `${p.name}.html`).sort(), readdirSync(snapshots).sort());
  for (const page of rendered) {
    assert.equal(page.html, readFileSync(path.join(snapshots, `${page.name}.html`), "utf8"), `${page.name} differs from 0.9.0`);
  }
});
