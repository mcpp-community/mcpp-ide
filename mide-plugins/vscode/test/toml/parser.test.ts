import assert from "node:assert/strict";
import test from "node:test";

import {
  parseMcppToml,
  type TomlKeyValueNode,
  type TomlSectionNode,
} from "../../src/toml/parser";

function sectionAt(lines: string[], index: number): TomlSectionNode {
  const node = parseMcppToml(lines).nodes[index];
  assert.equal(node?.type, "section");
  return node as TomlSectionNode;
}

function keyValueAt(lines: string[], index: number): TomlKeyValueNode {
  const node = parseMcppToml(lines).nodes[index];
  assert.equal(node?.type, "keyValue");
  return node as TomlKeyValueNode;
}

// ---- 段头解析 ----

test("解析普通段头及其范围", () => {
  const section = sectionAt(["[package]"], 0);
  assert.deepEqual(section.segments.map((s) => s.name), ["package"]);
  assert.equal(section.segments[0].quoted, false);
  assert.equal(section.isArray, false);
  assert.equal(section.open, false);
  assert.equal(section.line, 0);
  assert.deepEqual(section.range, { startLine: 0, startCharacter: 0, endLine: 0, endCharacter: 9 });
  assert.deepEqual(section.segments[0].range, { startLine: 0, startCharacter: 1, endLine: 0, endCharacter: 8 });
});

test("解析参数化段头 [targets.myapp]", () => {
  const section = sectionAt(["[targets.myapp]"], 0);
  assert.deepEqual(section.segments.map((s) => s.name), ["targets", "myapp"]);
  assert.deepEqual(section.segments[1].range, { startLine: 0, startCharacter: 9, endLine: 0, endCharacter: 14 });
});

test("解析单引号段 [target.'cfg(windows)'.build]", () => {
  const section = sectionAt(["[target.'cfg(windows)'.build]"], 0);
  assert.deepEqual(section.segments.map((s) => s.name), ["target", "cfg(windows)", "build"]);
  assert.equal(section.segments[1].quoted, true);
  assert.deepEqual(section.segments[1].range, { startLine: 0, startCharacter: 8, endLine: 0, endCharacter: 22 });
});

test("解析双引号段 [runtime.\"opengl.glx.driver\"]（带点的段名是一段）", () => {
  const section = sectionAt(['[runtime."opengl.glx.driver"]'], 0);
  assert.deepEqual(section.segments.map((s) => s.name), ["runtime", "opengl.glx.driver"]);
  assert.equal(section.segments[1].quoted, true);
  assert.deepEqual(section.segments[1].range, { startLine: 0, startCharacter: 9, endLine: 0, endCharacter: 28 });
});

test("解析数组表段头 [[build.flags]]", () => {
  const section = sectionAt(["[[build.flags]]"], 0);
  assert.equal(section.isArray, true);
  assert.equal(section.open, false);
  assert.deepEqual(section.segments.map((s) => s.name), ["build", "flags"]);
  assert.deepEqual(section.range, { startLine: 0, startCharacter: 0, endLine: 0, endCharacter: 15 });
});

test("未闭合段头解析为 open 状态且不抛错", () => {
  const document = parseMcppToml(["[dep", 'name = "x"']);
  const section = document.nodes[0] as TomlSectionNode;
  assert.equal(section.type, "section");
  assert.equal(section.open, true);
  assert.deepEqual(section.segments.map((s) => s.name), ["dep"]);
  // 后续行继续正常解析。
  const keyValue = document.nodes[1] as TomlKeyValueNode;
  assert.equal(keyValue.type, "keyValue");
  assert.deepEqual(keyValue.keyPath.map((s) => s.name), ["name"]);
});

// ---- 键值解析 ----

test("解析裸键字符串键值对及其范围", () => {
  const keyValue = keyValueAt(["[package]", 'name = "demo"'], 1);
  assert.deepEqual(keyValue.keyPath.map((s) => s.name), ["name"]);
  assert.deepEqual(keyValue.keyPath[0].range, { startLine: 1, startCharacter: 0, endLine: 1, endCharacter: 4 });
  assert.equal(keyValue.value?.kind, "string");
  assert.equal(keyValue.value?.open, false);
  assert.equal(keyValue.value?.text, "demo");
  assert.deepEqual(keyValue.value?.range, { startLine: 1, startCharacter: 7, endLine: 1, endCharacter: 13 });
  assert.deepEqual(keyValue.value?.contentRange, { startLine: 1, startCharacter: 8, endLine: 1, endCharacter: 12 });
});

test("解析引号键（键名带点仍是一段）", () => {
  const keyValue = keyValueAt(['"chriskohlhoff.asio" = "1.28"'], 0);
  assert.equal(keyValue.keyPath.length, 1);
  assert.equal(keyValue.keyPath[0].name, "chriskohlhoff.asio");
  assert.equal(keyValue.keyPath[0].quoted, true);
  assert.equal(keyValue.value?.text, "1.28");
});

test("解析点分键 capi.lua", () => {
  const keyValue = keyValueAt(['capi.lua = "1.0"'], 0);
  assert.deepEqual(keyValue.keyPath.map((s) => s.name), ["capi", "lua"]);
  assert.deepEqual(keyValue.keyPath[1].range, { startLine: 0, startCharacter: 5, endLine: 0, endCharacter: 8 });
});

test("解析整数与布尔值", () => {
  const integer = keyValueAt(["opt = 2"], 0);
  assert.equal(integer.value?.kind, "integer");
  assert.equal(integer.value?.text, "2");
  assert.deepEqual(integer.value?.range, { startLine: 0, startCharacter: 6, endLine: 0, endCharacter: 7 });

  const boolean = keyValueAt(["debug = true"], 0);
  assert.equal(boolean.value?.kind, "boolean");
  assert.equal(boolean.value?.text, "true");
  assert.deepEqual(boolean.value?.range, { startLine: 0, startCharacter: 8, endLine: 0, endCharacter: 12 });
});

test("解析跨行数组", () => {
  const keyValue = keyValueAt(["sources = [", '  "a",', '  "b",', "]"], 0);
  assert.equal(keyValue.value?.kind, "array");
  assert.equal(keyValue.value?.open, false);
  assert.deepEqual(keyValue.value?.range, { startLine: 0, startCharacter: 10, endLine: 3, endCharacter: 1 });
  assert.deepEqual(keyValue.value?.elements?.map((element) => element.text), ["a", "b"]);
});

test("未闭合数组解析为 open 状态", () => {
  const keyValue = keyValueAt(["flags = [ { glob = \"x\" },"], 0);
  assert.equal(keyValue.value?.kind, "array");
  assert.equal(keyValue.value?.open, true);
  assert.equal(keyValue.value?.elements?.length, 1);
  assert.equal(keyValue.value?.elements?.[0].kind, "inlineTable");
});

test("解析嵌套内联表", () => {
  const keyValue = keyValueAt(['dep = { version = "1", opts = { a = 1 } }'], 0);
  assert.equal(keyValue.value?.kind, "inlineTable");
  assert.equal(keyValue.value?.open, false);
  const entries = keyValue.value?.entries ?? [];
  assert.deepEqual(entries.map((entry) => entry.keyPath.map((s) => s.name)), [["version"], ["opts"]]);
  assert.equal(entries[0].value?.kind, "string");
  assert.equal(entries[1].value?.kind, "inlineTable");
  assert.equal(entries[1].value?.entries?.[0].value?.kind, "integer");
});

test("未闭合内联表解析为 open 状态并保留已输入条目", () => {
  const keyValue = keyValueAt(['dep = { version = "1"'], 0);
  assert.equal(keyValue.value?.kind, "inlineTable");
  assert.equal(keyValue.value?.open, true);
  assert.equal(keyValue.value?.entries?.length, 1);
  assert.deepEqual(keyValue.value?.entries?.[0].keyPath.map((s) => s.name), ["version"]);
});

// ---- 注释、字符串与行尾 ----

test("注释被跳过，字符串内的 # 不算注释", () => {
  const keyValue = keyValueAt(["# 顶部注释", 'name = "a # b" # 行尾注释'], 0);
  assert.equal(keyValue.value?.kind, "string");
  assert.equal(keyValue.value?.text, "a # b");
});

test("字符串内的转义引号不结束字符串", () => {
  const keyValue = keyValueAt(['desc = "a \\"b\\" c"'], 0);
  assert.equal(keyValue.value?.kind, "string");
  assert.equal(keyValue.value?.open, false);
  assert.equal(keyValue.value?.text, 'a \\"b\\" c');
});

test("解析三引号多行字符串", () => {
  const keyValue = keyValueAt(['text = """', "hello", '"""'], 0);
  assert.equal(keyValue.value?.kind, "string");
  assert.equal(keyValue.value?.multiline, true);
  assert.equal(keyValue.value?.open, false);
  assert.equal(keyValue.value?.text, "\nhello\n");
});

test("未闭合字符串解析为 open 状态", () => {
  const keyValue = keyValueAt(['name = "hel'], 0);
  assert.equal(keyValue.value?.kind, "string");
  assert.equal(keyValue.value?.open, true);
  assert.equal(keyValue.value?.text, "hel");
});

test("CRLF 行尾被剥除，列号按剥除后计算", () => {
  const document = parseMcppToml(["[package]\r", 'name = "demo"\r']);
  const section = document.nodes[0] as TomlSectionNode;
  assert.deepEqual(section.range, { startLine: 0, startCharacter: 0, endLine: 0, endCharacter: 9 });
  const keyValue = document.nodes[1] as TomlKeyValueNode;
  assert.deepEqual(keyValue.keyPath[0].range, { startLine: 1, startCharacter: 0, endLine: 1, endCharacter: 4 });
  assert.deepEqual(keyValue.value?.contentRange, { startLine: 1, startCharacter: 8, endLine: 1, endCharacter: 12 });
});

test("无法识别的垃圾行不抛错、不中断后续解析", () => {
  const document = parseMcppToml(["!!garbage!!", "[package]"]);
  const section = document.nodes.find((node) => node.type === "section") as TomlSectionNode;
  assert.deepEqual(section.segments.map((s) => s.name), ["package"]);
});

// ---- 回归：裸值停止字符 ----

test("裸值不停在 [ / { 之外的后续结构：x = 1 [dep", () => {
  const document = parseMcppToml(["x = 1 [dep"]);
  const keyValue = document.nodes[0] as TomlKeyValueNode;
  assert.equal(keyValue.type, "keyValue");
  assert.equal(keyValue.value?.kind, "integer");
  assert.equal(keyValue.value?.text, "1");
  assert.deepEqual(keyValue.value?.range, { startLine: 0, startCharacter: 4, endLine: 0, endCharacter: 5 });
  const section = document.nodes[1] as TomlSectionNode;
  assert.equal(section.type, "section");
  assert.equal(section.open, true);
  assert.deepEqual(section.segments.map((s) => s.name), ["dep"]);
});

test("裸值在 { 前停止：y = 2 {k = 1}", () => {
  const keyValue = keyValueAt(["y = 2 {k = 1}"], 0);
  assert.equal(keyValue.value?.kind, "integer");
  assert.equal(keyValue.value?.text, "2");
  assert.deepEqual(keyValue.value?.range, { startLine: 0, startCharacter: 4, endLine: 0, endCharacter: 5 });
});
