// The wire against the real backend: frames split anywhere reassemble; `initialize` over the mock
// mcpp finds the project; a build's notifications arrive and end in job/finished.
import * as assert from "node:assert/strict";
import { test } from "node:test";

import { Backend, encode, FrameReader } from "../../src/backend/client";
import { built, project } from "./harness";

test("frames reassemble whatever the chunking", () => {
  const all = Buffer.concat([Buffer.from("Junk: 1\r\n\r\n"), encode({ a: 1 }), encode({ b: "é" })]);
  const r = new FrameReader();
  const bodies: string[] = [];
  for (let i = 0; i < all.length; i++) bodies.push(...r.feed(all.subarray(i, i + 1)));
  assert.deepEqual(bodies.map((b) => JSON.parse(b)), [{ a: 1 }, { b: "é" }]);
});

test("a session over the mock mcpp", async (t) => {
  const backend = built("mcpp-ide"), mock = built("mcpp-ide-mock-mcpp");
  if (!backend || !mock) { t.skip("build the backend first (mcpp build)"); return; }
  const root = project();
  const b = new Backend({ program: backend });
  const init = await b.request<{ projects: { root: string }[]; mcpp: { contract: boolean } }>("initialize", {
    editor: "vscode", settings: { "mcpp.path": mock }, workspaceFolders: [root],
  });
  assert.equal(init.projects.length, 1);
  assert.equal(init.mcpp.contract, true);
  const seen: string[] = [];
  b.notifications.on("*", (method: string) => seen.push(method));
  const finished = new Promise((resolve) => b.notifications.on("job/finished", resolve));
  const { jobId } = await b.request<{ jobId: string }>("job/start", { kind: "build", root });
  assert.match(jobId, /^job-/);
  const done = (await finished) as { status: string };
  assert.equal(done.status, "ok");
  for (const m of ["job/started", "job/plan", "job/progress", "job/artifact"]) assert.ok(seen.includes(m), m);
  await assert.rejects(b.request("no/such/method"), /unknown method/);
  await b.stop();
  assert.equal(b.running, false);
});
