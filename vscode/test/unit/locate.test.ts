// Where the backend and mcppls are found.
import * as assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";

import { locateBackend, onPath, platformDir } from "../../src/backend/locate";
import { mcpplsCandidates } from "../../src/backend/mcppls";

test("the configured path wins, then the bundled program", () => {
  const ext = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-ext-"));
  assert.deepEqual(locateBackend({ configured: "/x/mcpp-ide", extensionRoot: ext }), { program: "/x/mcpp-ide", source: "setting" });
  const dir = path.join(ext, "bin", platformDir("linux", "x64"));
  fs.mkdirSync(dir, { recursive: true });
  fs.writeFileSync(path.join(dir, "mcpp-ide"), "");
  const found = locateBackend({ extensionRoot: ext, platform: "linux", arch: "x64", env: { PATH: "" }, home: "/nonexistent" });
  assert.equal(found?.source, "bundled");
  assert.equal(platformDir("win32", "x64"), "win32-x64");
});

test("PATH lookup appends .exe on Windows", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-path-"));
  fs.writeFileSync(path.join(dir, "mcpp-ide.exe"), "");
  assert.equal(onPath("mcpp-ide", { PATH: dir }, "win32"), path.join(dir, "mcpp-ide.exe"));
  assert.equal(onPath("mcpp-ide", { PATH: dir }, "linux"), undefined);
});

test("mcppls: the extension's payload names its server", () => {
  const ext = fs.mkdtempSync(path.join(os.tmpdir(), "mcppls-ext-"));
  fs.mkdirSync(path.join(ext, "payload"));
  fs.writeFileSync(path.join(ext, "payload", "payload.json"), JSON.stringify({ server: { path: "bin/mcppls" } }));
  assert.deepEqual(mcpplsCandidates(ext, {}), [path.join(ext, "payload", "bin", "mcppls")]);
  assert.deepEqual(mcpplsCandidates(undefined, { MCPPLS_SERVER: "/s/mcppls" }), ["/s/mcppls"]);
});
