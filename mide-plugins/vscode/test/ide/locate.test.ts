// Where the backend and mcppls are found.
import * as assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";

import { locateBackend, okrunDir, onPath } from "../../src/ide/backend/locate";
import { mcpplsCandidates } from "../../src/ide/backend/mcppls";

// PATH is this host's (its separator, its suffix), so these use the host's own system.
const host = process.platform;
const exe = host === "win32" ? ".exe" : "";

test("the configured path wins: a program, or a bundle run by okrun", () => {
  const ext = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-ext-"));
  assert.deepEqual(locateBackend({ configured: "/x/mcpp-ide", extensionRoot: ext }), { program: "/x/mcpp-ide", args: [], source: "setting" });
  const bin = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-okrun-"));
  fs.writeFileSync(path.join(bin, `okrun${exe}`), "");
  assert.deepEqual(locateBackend({ configured: "/x/mcpp-ide.okb", extensionRoot: ext, platform: host, arch: "x64", env: { PATH: bin } }),
                   { program: path.join(bin, `okrun${exe}`), args: ["/x/mcpp-ide.okb"], source: "setting" });
});

test("the bundle runs under the okrun carried for this system, else okrun on PATH", () => {
  const ext = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-okb-"));
  const carried = okrunDir(host, "x64");
  fs.mkdirSync(path.join(ext, "backend", "okrun", carried), { recursive: true });
  fs.writeFileSync(path.join(ext, "backend", "mcpp-ide.okb"), "");
  fs.writeFileSync(path.join(ext, "backend", "okrun", carried, `okrun${exe}`), "");
  const here = locateBackend({ extensionRoot: ext, platform: host, arch: "x64", env: { PATH: "" }, home: "/nonexistent" });
  assert.deepEqual(here, { program: path.join(ext, "backend", "okrun", carried, `okrun${exe}`), args: [path.join(ext, "backend", "mcpp-ide.okb")], source: "bundle" });
  // No okrun carried for the other architecture: okrun on PATH runs the bundle.
  const bin = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-okrun-"));
  fs.writeFileSync(path.join(bin, `okrun${exe}`), "");
  const other = locateBackend({ extensionRoot: ext, platform: host, arch: "arm64", env: { PATH: bin }, home: "/nonexistent" });
  assert.deepEqual(other?.args, [path.join(ext, "backend", "mcpp-ide.okb")]);
  assert.equal(other?.program, path.join(bin, `okrun${exe}`));
  // No okrun at all: nothing in the package starts.
  const none = locateBackend({ extensionRoot: ext, platform: host, arch: "arm64", env: { PATH: "" }, home: "/nonexistent" });
  assert.equal(none, undefined);
  assert.equal(okrunDir("linux", "x64"), "linux-x86_64");
  assert.equal(okrunDir("win32", "x64"), "windows-x86_64");
  assert.equal(okrunDir("darwin", "arm64"), "macos-aarch64");
});

test("PATH lookup appends .exe on Windows", () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-path-"));
  fs.writeFileSync(path.join(dir, "mcpp-ide.exe"), "");
  assert.equal(onPath("mcpp-ide", { PATH: dir }, "win32"), path.join(dir, "mcpp-ide.exe"));
  assert.equal(onPath("mcpp-ide", { PATH: dir }, "linux"), undefined);
});

test("mcppls: the extension's payload is the candidate, for the backend to start its server", () => {
  const ext = fs.mkdtempSync(path.join(os.tmpdir(), "mcppls-ext-"));
  assert.deepEqual(mcpplsCandidates(ext, {}), [], "no payload, nothing");
  fs.mkdirSync(path.join(ext, "payload"));
  fs.writeFileSync(path.join(ext, "payload", "payload.json"), JSON.stringify({ server: { path: "bin/mcppls.okb", runner: "bin/okrun" } }));
  assert.deepEqual(mcpplsCandidates(ext, {}), [path.join(ext, "payload")]);
  assert.deepEqual(mcpplsCandidates(ext, { MCPPLS_PAYLOAD: path.join(ext, "payload") }), [path.join(ext, "payload")]);
  assert.deepEqual(mcpplsCandidates(undefined, { MCPPLS_SERVER: "/s/mcppls" }), ["/s/mcppls"]);
});
