/**
 * The C++ Modules extension, faked for the Extension Host tests.
 *
 * Five variants, because the promise is that *any* version of the dependency
 * keeps this extension usable — a full one, one that only offers the old restart,
 * one that renamed its command, one that exposes no state API, and one whose
 * state API throws. The stub is generated rather than checked in five times so
 * the command list has a single source here.
 *
 * Each variant carries a server in its payload, as the real extension does: what the backend asks
 * (`mcppls --version`, and `mcppls status` from 0.0.20) to learn mcppls's actions and their VS Code
 * commands (design 2026-10-30 part 2 §5.3). The full one is 0.0.20 and states its actions; the
 * partial one is 0.0.18, the oldest supported, read through the backend's frozen table; the others
 * are 0.0.19, read the same way.
 */

export type StubVariant = "full" | "partial" | "renamed" | "noapi" | "throwing";

export const STUB_VARIANTS: readonly StubVariant[] = ["full", "partial", "renamed", "noapi", "throwing"];

/** Every command id this extension forwards, as the real extension registers them. */
const COMMANDS = [
  "mcppls.restartServer",
  "mcppls.reloadBuildDescription",
  "mcppls.selectContext",
  "mcppls.showModuleGraph",
  "mcppls.showLogs",
  "mcppls.restartClangd",
  "mcppls.resetWorkspaceCache",
  "mcppls.collectReport",
  "mcppls.exportDiagnosticBundle",
  "mcppls.runBuildToolInTerminal",
  "mcppls.turnOffOtherCppFeatures",
  "mcppls.restoreOtherCppFeatures",
  "mcppls.turnOffInWorkspace",
  "mcppls.turnOnInWorkspace",
  "mcppls.installCommandLineTools",
  "mcppls.review.run",
  "mcppls.review.clear",
];

function commandsFor(variant: StubVariant): string[] {
  switch (variant) {
    case "partial":
      return ["mcppls.restartServer"];
    case "renamed":
      return ["mcppls.restartServer2"];
    default:
      return COMMANDS;
  }
}

/** A ready status, shaped as S3 documents it. */
const READY_STATUS = JSON.stringify({
  state: "ready",
  project: { root: "file:///e2e", source: "mcpp", level: 3, tier: 1 },
  profile: { kind: "build-toolchain", compiler: "clang 22.1.8", stdlib: "libc++ 22.1.8", target: "x86_64-linux-gnu" },
  engine: { name: "clangd", version: "23.1.0" },
  engines: [{ name: "clangd", version: "23.1.0", role: "core", state: "ready" }],
});

export function stubPackageJson(variant: StubVariant): string {
  return `${JSON.stringify(
    {
      name: "mcpp-language-server",
      displayName: "C++ Modules Language Server stub",
      version: "0.0.0",
      publisher: "sunrisepeak",
      engines: { vscode: "^1.91.0" },
      main: "./extension.js",
      activationEvents: commandsFor(variant).map((command) => `onCommand:${command}`),
      contributes: { commands: commandsFor(variant).map((command) => ({ command, title: command })) },
    },
    null,
    2,
  )}\n`;
}

export function stubExtensionJs(variant: StubVariant): string {
  const commands = JSON.stringify(commandsFor(variant), null, 2);
  const stateBody =
    variant === "throwing"
      ? `throw new Error("the stub state reader always throws");`
      : `return ${READY_STATUS};`;
  const exportsBody =
    variant === "noapi"
      ? `module.exports = { activate };`
      : `module.exports = { activate };`;
  const apiReturn = variant === "noapi" ? "" : `  return { lastStatus: () => { ${stateBody} }, statusBarText: () => "ready" };`;

  return `const fs = require("node:fs");
const vscode = require("vscode");

// Records every forwarded call so the suite can assert what this extension asked for.
function record(command) {
  const logPath = process.env.MCPP_E2E_MCPPLS_LOG;
  if (typeof logPath === "string") {
    fs.appendFileSync(logPath, command + "\\n");
  }
}

function activate(context) {
  for (const command of ${commands}) {
    context.subscriptions.push(
      vscode.commands.registerCommand(command, () => {
        record(command);
        return undefined;
      }),
    );
  }
${apiReturn}
}

${exportsBody}
`;
}

/** The server's version: `status` exists from 0.0.20. */
export function stubServerVersion(variant: StubVariant): string {
  return variant === "full" ? "0.0.20" : variant === "partial" ? "0.0.18" : "0.0.19";
}

/** mcppls 0.0.20's actions under VS Code, as `mcppls status` states them (mcppls S3-8-7). */
const STATUS_ACTIONS: ReadonlyArray<[string, string, unknown[]?]> = [
  ["restart", "mcppls.restartServer"],
  ["reload", "mcppls.reloadBuildDescription"],
  ["restartEngine", "mcppls.restartClangd"],
  ["resetCache", "mcppls.resetWorkspaceCache"],
  ["showLog", "mcppls.showLogs"],
  ["openLogDirectory", "mcppls.revealCacheDirectory", ["logs"]],
  ["openCacheDirectory", "mcppls.revealCacheDirectory"],
  ["collectReport", "mcppls.collectReport"],
  ["exportDiagnostics", "mcppls.exportDiagnosticBundle"],
  ["selectContext", "mcppls.selectContext"],
  ["moduleGraph", "mcppls.showModuleGraph"],
  ["runBuildTool", "mcppls.runBuildToolInTerminal"],
  ["installTools", "mcppls.installCommandLineTools"],
  ["reviewRun", "mcppls.review.run"],
  ["reviewClear", "mcppls.review.clear"],
  ["turnOffInWorkspace", "mcppls.turnOffInWorkspace"],
  ["turnOnInWorkspace", "mcppls.turnOnInWorkspace"],
  ["turnOffOtherCppFeatures", "mcppls.turnOffOtherCppFeatures"],
  ["restoreOtherCppFeatures", "mcppls.restoreOtherCppFeatures"],
];

/** The server's program, a node script: `--version`, and `status --format json --root <dir>`. */
export function stubServerJs(variant: StubVariant): string {
  const version = stubServerVersion(variant);
  const actions = STATUS_ACTIONS.map(([id, vscode, args]) => ({ id, title: id, vscode, ...(args ? { vscodeArguments: args } : {}) }));
  return `const args = process.argv.slice(2);
if (args[0] === "--version") {
  process.stdout.write("mcppls ${version}\\n");
} else if (args[0] === "status" && ${JSON.stringify(variant === "full")}) {
  const root = args[args.indexOf("--root") + 1];
  process.stdout.write(JSON.stringify({
    schema: 1,
    server: { version: "${version}", platform: "test" },
    capabilities: ["status"],
    workspace: { root, state: "ready" },
    instances: [],
    actions: ${JSON.stringify(actions)},
  }) + "\\n");
} else {
  process.stderr.write("mcppls: unknown command\\n");
  process.exit(2);
}
`;
}
