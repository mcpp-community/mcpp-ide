// Where the backend and mcppls are found.
import * as assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";

import { locateBackend, okrunDir, onPath } from "../../src/backend/locate";
import { mcpplsCandidates } from "../../src/backend/mcppls";

test("the configured path wins: a program, or a bundle run by okrun", () => {
  const ext = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-ext-"));
  assert.deepEqual(locateBackend({ configured: "/x/mcpp-ide", extensionRoot: ext }), { program: "/x/mcpp-ide", args: [], source: "setting" });
  const bin = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-okrun-"));
  fs.writeFileSync(path.join(bin, "okrun"), "");
  assert.deepEqual(locateBackend({ configured: "/x/mcpp-ide.okb", extensionRoot: ext, platform: "linux", arch: "x64", env: { PATH: bin } }),
                   { program: path.join(bin, "okrun"), args: ["/x/mcpp-ide.okb"], source: "setting" });
});

test("the bundle runs under the okrun carried for this system, else okrun on PATH", () => {
  const ext = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-okb-"));
  fs.mkdirSync(path.join(ext, "backend", "okrun", "linux-x86_64"), { recursive: true });
  fs.writeFileSync(path.join(ext, "backend", "mcpp-ide.okb"), "");
  fs.writeFileSync(path.join(ext, "backend", "okrun", "linux-x86_64", "okrun"), "");
  const linux = locateBackend({ extensionRoot: ext, platform: "linux", arch: "x64", env: { PATH: "" }, home: "/nonexistent" });
  assert.deepEqual(linux, { program: path.join(ext, "backend", "okrun", "linux-x86_64", "okrun"), args: [path.join(ext, "backend", "mcpp-ide.okb")], source: "bundle" });
  // No okrun carried for macOS: okrun on PATH runs the bundle.
  const bin = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-okrun-"));
  fs.writeFileSync(path.join(bin, "okrun"), "");
  const mac = locateBackend({ extensionRoot: ext, platform: "darwin", arch: "arm64", env: { PATH: bin }, home: "/nonexistent" });
  assert.deepEqual(mac?.args, [path.join(ext, "backend", "mcpp-ide.okb")]);
  assert.equal(mac?.program, path.join(bin, "okrun"));
  // No okrun at all: nothing in the package starts.
  const native = locateBackend({ extensionRoot: ext, platform: "linux", arch: "arm64", env: { PATH: "" }, home: "/nonexistent" });
  assert.equal(native, undefined);
  assert.equal(okrunDir("win32", "x64"), "windows-x86_64");
  assert.equal(okrunDir("darwin", "arm64"), "macos-aarch64");
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
