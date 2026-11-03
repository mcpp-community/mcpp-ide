/**
 * Parity gate (a): everything mcpp-vscode 0.9.0 contributed is contributed unchanged — every
 * command, keybinding, menu entry and setting with its title, icon, `when`, default and order, every
 * view, language, grammar and colour — and every string it showed reads the same. The manifest
 * may add (the flows 1.1.0 adds); it may not change or drop. `fixtures/0.9.0/` is 0.9.0's manifest
 * and strings, from the v0.9.0 tag.
 */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";
import test from "node:test";

const root = process.cwd();
const read = (file: string): any => JSON.parse(readFileSync(file, "utf8"));
const old = read(path.join(root, "test/parity/fixtures/0.9.0/package.json"));
const now = read(path.join(root, "package.json"));
const canon = (value: unknown): string => JSON.stringify(value, (_key, v) =>
  v && typeof v === "object" && !Array.isArray(v) ? Object.fromEntries(Object.entries(v).sort(([a], [b]) => a.localeCompare(b))) : v);

test("every command 0.9.0 contributed is contributed unchanged", () => {
  const byId = new Map<string, unknown>(now.contributes.commands.map((c: { command: string }) => [c.command, c]));
  for (const command of old.contributes.commands) assert.equal(canon(byId.get(command.command)), canon(command), command.command);
});

test("every keybinding and menu entry 0.9.0 contributed is contributed unchanged", () => {
  const keys = new Set(now.contributes.keybindings.map(canon));
  for (const binding of old.contributes.keybindings) assert.ok(keys.has(canon(binding)), canon(binding));
  for (const [menu, entries] of Object.entries<unknown[]>(old.contributes.menus)) {
    const present = new Set((now.contributes.menus[menu] ?? []).map(canon));
    for (const entry of entries) assert.ok(present.has(canon(entry)), `${menu}: ${canon(entry)}`);
  }
});

test("every setting 0.9.0 contributed keeps its type, default, scope, order and strings", () => {
  const settings = now.contributes.configuration.properties;
  for (const [key, property] of Object.entries(old.contributes.configuration.properties)) {
    assert.equal(canon(settings[key]), canon(property), key);
  }
  assert.equal(now.contributes.configuration.title, old.contributes.configuration.title);
});

test("the views, languages, grammars, colours and the rest of the manifest are 0.9.0's", () => {
  for (const key of ["viewsContainers", "views", "languages", "grammars", "colors", "problemMatchers", "viewsWelcome", "configurationDefaults"]) {
    assert.equal(canon(now.contributes[key]), canon(old.contributes[key]), key);
  }
  for (const key of ["name", "publisher", "displayName", "description", "icon", "engines", "extensionDependencies", "capabilities", "main"]) {
    assert.equal(canon(now[key]), canon(old[key]), key);
  }
  for (const event of old.activationEvents) assert.ok(now.activationEvents.includes(event), event);
});

test("every string 0.9.0 showed reads the same, in English and in Chinese", () => {
  for (const file of ["package.nls.json", "package.nls.zh-cn.json"]) {
    const before = read(path.join(root, "test/parity/fixtures/0.9.0", file));
    const after = read(path.join(root, file));
    for (const [key, value] of Object.entries(before)) assert.equal(after[key], value, `${file}: ${key}`);
  }
  // The runtime strings (messages, dialogs, the pages' labels) too.
  const runtime = read(path.join(root, "test/parity/fixtures/0.9.0/runtime.zh-cn.json"));
  const translations = read(path.join(root, "data/i18n/zh-cn.json"));
  for (const [key, value] of Object.entries(runtime)) assert.equal(translations[key], value, `runtime: ${key}`);
});
