#!/usr/bin/env node

// The mcpp the e2e suite points the extension at: it records every command it is given (the suite
// reads the log), answers the two the suite drives (`build`, and the configure-only build the
// language service asks for), and answers mcpp's machine contract -- the version, the protocol,
// the toolchain, cache, index and package lists -- from the backend's recorded fixtures
// (MCPP_E2E_FIXTURES, protocol/fixtures). The backend reaches mcpp through this script as the
// extension does.
const fs = require("node:fs");
const path = require("node:path");

const logPath = process.env.MCPP_E2E_LOG;
const args = process.argv.slice(2);
const command = args.join(" ");
if (typeof logPath === "string") {
  fs.appendFileSync(logPath, `${command}\n`);
}

if (command === "build --configure-only") {
  // 使用 syntax-only CDB，避免 smoke test 依赖宿主 clangd 或真实编译器。
  const cwd = process.cwd();
  fs.writeFileSync(path.join(cwd, "compile_commands.json"), JSON.stringify([{
    directory: cwd,
    file: path.join(cwd, "main.cpp"),
    arguments: ["gcc", "-std=c++23", "-c", path.join(cwd, "main.cpp")],
  }]));
  process.exit(0);
}
if (command === "build") {
  process.exit(0);
}

const fixtures = process.env.MCPP_E2E_FIXTURES;
const answers = [
  [["--version"], "version.txt"],
  [["--protocol-version"], "protocol-version.json"],
  [["toolchain", "list"], "toolchain-list.json"],
  [["cache", "list"], "cache-list.json"],
  [["self", "env"], "self-env.json"],
  [["index", "list"], "index-list.json"],
  [["search"], "search.json"],
  [["pack", "--list-formats"], "pack-formats.json"],
  [["pack", "--help"], "pack-help.txt"],
];
if (typeof fixtures === "string" && fixtures.length > 0) {
  for (const [words, file] of answers) {
    if (words.every((word, i) => args[i] === word || (i > 0 && args.includes(word)))) {
      process.stdout.write(fs.readFileSync(path.join(fixtures, file), "utf8"));
      process.exit(0);
    }
  }
}
process.exit(1);
