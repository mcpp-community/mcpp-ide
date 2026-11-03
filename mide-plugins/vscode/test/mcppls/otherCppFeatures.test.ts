import assert from "node:assert/strict";
import test from "node:test";

import { OTHER_CPP_SETTINGS, localOtherCppFeatures } from "../../src/mcppls/otherCppFeatures";

const installed = (...ids: string[]) => (id: string) => ids.includes(id);
const settings = (values: Record<string, unknown>) => (section: string, key: string) => values[`${section}.${key}`];

test("the local read finds each candidate that is installed and not turned off", () => {
  const found = localOtherCppFeatures(
    installed("ms-vscode.cpptools", "llvm-vs-code-extensions.vscode-clangd", "ccls-project.ccls"),
    settings({ "C_Cpp.intelliSenseEngine": "default" }),
  );
  assert.deepEqual(found, {
    active: [
      { extensionId: "ms-vscode.cpptools", displayName: "C/C++", settable: true },
      { extensionId: "llvm-vs-code-extensions.vscode-clangd", displayName: "clangd", settable: true },
      { extensionId: "ccls-project.ccls", displayName: "ccls", settable: false },
    ],
    canRestore: false,
  });
});

test("the local read leaves out what is turned off by its own setting, or not installed", () => {
  const found = localOtherCppFeatures(
    installed("ms-vscode.cpptools", "llvm-vs-code-extensions.vscode-clangd"),
    settings({ "C_Cpp.intelliSenseEngine": "disabled", "clangd.enable": false }),
  );
  assert.deepEqual(found.active, []);
  assert.deepEqual(OTHER_CPP_SETTINGS, ["C_Cpp.intelliSenseEngine", "clangd.enable"]);
});
