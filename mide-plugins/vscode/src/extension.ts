import * as path from "node:path";
import * as os from "node:os";
import type { ProjectMcpp } from "./ide/protocol";
import * as vscode from "vscode";

import { McppCliController } from "./cli/controller";
import { findCurrentProject } from "./projects/current";
import { runMcpp, useMcppExecutor, useMcpplsExecutor } from "./cli/process";
import { parseProtocolInfo } from "./cli/protocol";
import { buildSelfCheckText } from "./cli/selfCheck";
import { CLI_COMMANDS, LEGACY_LANGUAGE_SERVER_COMMANDS, TOOL_COMMANDS } from "./commands/ids";
import type { McppProjectDiscovery } from "./projects/discovery";
import { MCPP_MANIFEST_GLOB, registerInProjectContext } from "./projects/context";
import {
  createLanguageServerBridge,
  type LanguageServerBridge,
  type LanguageServerCommandResult,
} from "./mcppls/bridge";
import { compareVersions } from "./library/xpkg";
import { CAPABILITIES, MCPPLS_EXTENSION_ID, VERIFIED_MCPPLS_MINIMUM, VERIFIED_MCPPLS_RANGE } from "./mcppls/contract";
import { formatResult } from "./mcppls/messages";
import { describeState } from "./mcppls/state";
import {
  languageServerEnabled,
  languageServerInstalled,
  languageServerServes,
  languageServerVersion,
  readLanguageServerState,
} from "./mcppls/stateSource";
import { registerBuildScriptProviders } from "./buildscript/providers";
import { registerTomlProviders } from "./toml/providers";
import {
  buildModuleSetupPlan,
  executeModuleSetup,
  moduleSetupConfirmation,
  type ModuleSetupDecision,
  type ModuleSetupStepResult,
} from "./workflows/moduleSetup";
import type { ProjectTaskKind, TaskCompletion } from "./cli/tasks";
import { Debugging } from "./ide/debug";
import { packArguments, selectTarget } from "./ide/flows";
import { Jobs } from "./ide/jobs";
import type { IdeProjects } from "./ide/projects";
import { Session } from "./ide/session";
import { Testing } from "./ide/testing";
import { changedSettings, onDidChange as onConfigurationChanged, read } from "./config/access";
import { applyRenames, pendingRenames, renamePrompt } from "./config/migrate";
import { languagePreference, setLanguagePreference, t, type LanguagePreference } from "./i18n/t";
import { CACHE_VIEW_FOCUS_COMMAND } from "./cache/cachePanel";
import { readCacheSnapshot } from "./cache/cacheView";
import { registerPages } from "./pages";
import { registerLanguageServerCommands } from "./views/languageServerView";

/**
 * The commands an extension declares in its own `package.json`, read without
 * activating it. `undefined` means "no static information", which the capability
 * probe treats as "assume it works until a call says otherwise".
 */
function declaredCommandsOf(id: string): readonly string[] | undefined {
  const extension = vscode.extensions.getExtension(id);
  const commands = (extension?.packageJSON as { contributes?: { commands?: Array<{ command?: string }> } })
    ?.contributes?.commands;
  if (!Array.isArray(commands)) {
    return undefined;
  }
  return commands
    .map((entry) => entry.command)
    .filter((command): command is string => typeof command === "string");
}

/** When the language service was last asked to reload, for the self-check. */
let lastRefresh: { at: string; state: string; command?: string } | undefined;

/** `mcpp.ui.language` decides which of our strings the user sees. */
function applyLanguagePreference(): void {
  setLanguagePreference(read<LanguagePreference>("mcpp.ui.language"));
}


function outputText(output: vscode.OutputChannel, line: string): void {
  try {
    output.appendLine(line);
  } catch {
    // The output channel can close before an async task finishes during shutdown.
  }
}

function resultText(output: vscode.OutputChannel, result: LanguageServerCommandResult): void {
  const formatted = formatResult(result);
  outputText(output, `[C++ Modules] ${formatted.message}${formatted.hint === undefined ? "" : ` ${formatted.hint}`}`);
  if (result.state === "unavailable") {
    const install = t("Install extension");
    void vscode.window.showWarningMessage(formatted.message, install).then((choice) => {
      if (choice === install) {
        void vscode.commands.executeCommand("workbench.extensions.search", `@id:${MCPPLS_EXTENSION_ID}`);
      }
    });
    return;
  }
  if (formatted.severity === "warning") {
    void vscode.window.showWarningMessage(formatted.message);
  } else if (formatted.severity === "error") {
    void vscode.window.showErrorMessage(formatted.message);
  }
}

function moduleSetupBlockedMessage(reason: Extract<ModuleSetupDecision, { kind: "blocked" }>["reason"]): string {
  switch (reason) {
    case "untrusted":
      return t("This workspace is not trusted, so mcpp will not run and the C++ Modules language service will not refresh. Trust the workspace first.");
    case "busy":
      return t("An mcpp operation is already running; wait for it to finish and try again.");
  }
}

function taskCompletionText(completion: TaskCompletion): string {
  if (completion.state === "cancelled") {
    return t("The build was cancelled; the C++ Modules language service was not refreshed.");
  }
  if (completion.state === "failed") {
    return t("The mcpp build failed (exit code {0}); the C++ Modules language service re-read the existing build description.", completion.exitCode ?? t("unknown"));
  }
  return t("The mcpp build finished; the C++ Modules language service has been refreshed.");
}

async function autoConfigureModulesWizard(
  bridge: LanguageServerBridge,
  cliController: McppCliController,
  output: vscode.OutputChannel,
): Promise<void> {
  if (!vscode.workspace.isTrusted) {
    await vscode.window.showWarningMessage(moduleSetupBlockedMessage("untrusted"));
    return;
  }
  if (cliController.isBusy()) {
    await vscode.window.showWarningMessage(moduleSetupBlockedMessage("busy"));
    return;
  }

  const decision = buildModuleSetupPlan(true, false);
  if (decision.kind === "blocked") {
    await vscode.window.showWarningMessage(moduleSetupBlockedMessage(decision.reason));
    return;
  }
  const confirmation = moduleSetupConfirmation();
  // One definition: the label is shown *and* the reply is compared against it.
  const confirmLabel = t("Confirm one-click setup");
  const choice = await vscode.window.showWarningMessage(
    confirmation.message,
    { modal: true, detail: confirmation.detail },
    confirmLabel,
  );
  if (choice !== confirmLabel) {
    return;
  }

  const outcome = await executeModuleSetup(decision, {
    build: async (): Promise<ModuleSetupStepResult> => {
      const completion = await cliController.runProjectTask("build", { notify: false });
      if (completion === undefined) {
        return {
          stage: "build",
          state: "not-started",
          detail: t("mcpp build was not started."),
        };
      }
      return {
        stage: "build",
        state: completion.state,
        exitCode: completion.exitCode,
      };
    },
    refreshLanguageServer: async (): Promise<ModuleSetupStepResult> => {
      const result = await bridge.refreshLanguageServerAfterBuild();
      resultText(output, result);
      return {
        stage: "language-server",
        state: result.state === "completed" ? "succeeded" : "failed",
        detail: result.state === "completed" ? undefined : formatResult(result).message,
      };
    },
  });

  const buildStep = outcome.steps[0];
  if (outcome.state === "succeeded") {
    await vscode.window.showInformationMessage(
      taskCompletionText(buildStep?.state === "failed" ? { state: "failed", exitCode: buildStep.exitCode } : { state: "succeeded" }),
    );
  } else if (outcome.state === "cancelled") {
    await vscode.window.showWarningMessage(taskCompletionText({ state: "cancelled" }));
  } else {
    await vscode.window.showErrorMessage(outcome.steps.at(-1)?.detail ?? t("Configuring the C++ Modules language service failed."));
  }
}

/** What the extension hands to whoever activates it (its own tests): the backend session. */
export interface McppApi {
  session: Session;
}

export async function activate(extensionContext: vscode.ExtensionContext): Promise<McppApi> {
  const output = vscode.window.createOutputChannel("mcpp");

  // Language first: everything below may want to speak to the user.
  applyLanguagePreference();
  extensionContext.subscriptions.push(onConfigurationChanged(applyLanguagePreference));

  const bridge = createLanguageServerBridge({
    extensionInstalled: (id) => vscode.extensions.getExtension(id) !== undefined,
    declaredCommands: declaredCommandsOf,
    activateExtension: async (id) => {
      await vscode.extensions.getExtension(id)?.activate();
    },
    executeCommand: <T>(command: string, ...args: unknown[]) => vscode.commands.executeCommand<T>(command, ...args),
  });
  const runGuarded = (operation: () => Promise<void>): (() => Promise<void>) => async () => {
    try {
      await operation();
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error);
      outputText(output, t("Unexpected error: {0}", message));
      void vscode.window.showErrorMessage(t("mcpp: {0}", message));
    }
  };
  const invokeLanguageServer = (
    operation: () => Promise<LanguageServerCommandResult>,
  ): (() => Promise<void>) => runGuarded(async () => {
    resultText(output, await operation());
  });

  const manifestWatcher = vscode.workspace.createFileSystemWatcher(MCPP_MANIFEST_GLOB);
  const inProjectContext = registerInProjectContext({
    currentProject: findCurrentProject,
    setContextValue: (key, value) => vscode.commands.executeCommand("setContext", key, value),
    subscribe: (listener) => [
      vscode.window.onDidChangeActiveTextEditor(listener),
      vscode.workspace.onDidChangeWorkspaceFolders(listener),
      manifestWatcher.onDidCreate(listener),
      manifestWatcher.onDidDelete(listener),
    ],
  });

  const afterProjectTask = async (
    _project: McppProjectDiscovery,
    _kind: ProjectTaskKind,
    completion: TaskCompletion,
  ): Promise<void> => {
    if (completion.state === "cancelled") {
      return;
    }
    const mode = read<string>("mcpp.languageService.refreshAfterBuild");
    if (mode === "off") {
      lastRefresh = { at: new Date().toISOString(), state: "skipped (mcpp.languageService.refreshAfterBuild = off)" };
      return;
    }
    const result =
      mode === "restart" ? await bridge.restartLanguageServer() : await bridge.refreshLanguageServerAfterBuild();
    lastRefresh = { at: new Date().toISOString(), state: result.state, command: result.command };
    resultText(output, result);
    if (completion.state === "succeeded" && result.state === "completed") {
      await vscode.window.showInformationMessage(taskCompletionText(completion));
    } else if (completion.state === "failed" && result.state === "completed") {
      await vscode.window.showWarningMessage(taskCompletionText(completion));
    }
  };

  const projectMcpp = new Map<string, ProjectMcpp>();
  const cliController = new McppCliController({
    output,
    extensionUri: extensionContext.extensionUri,
    currentProject: findCurrentProject,
    afterProjectTask,
    isTrusted: () => vscode.workspace.isTrusted,
    languageServerSummary: () => {
      const view = readLanguageServerState();
      return view.available ? view.state : undefined;
    },
    projectMcpp: () => {
      const project = findCurrentProject();
      return project === undefined ? undefined : projectMcpp.get(project.root);
    },
  });

  // ── the mcpp-ide backend ─────────────────────────────────────────────────
  // The backend (`mcpp-ide serve`, carried as one ok-image bundle and every system's runner) answers
  // the views' data and the flows 0.9.0 did not have: debugging, the Testing view, the targets and
  // target sets, packing. It starts in the background; nothing above waits for it.
  const session = new Session(extensionContext, output);
  const ideProjects: IdeProjects = {
    current: () => findCurrentProject(),
    all: () => session.info?.projects ?? [],
  };
  const jobs = new Jobs(session);
  // The project's mcpp (§4.4): asked when the backend starts, when the current project changes,
  // after the window regains focus, and when a job's envelope named another version.
  const refreshProjectMcpp = async (): Promise<void> => {
    const project = findCurrentProject();
    if (project === undefined || !session.running) return;
    try {
      projectMcpp.set(project.root, await session.request<ProjectMcpp>("project/mcpp", { root: project.root }));
    } catch {
      projectMcpp.delete(project.root);
    }
    cliController.refreshStatus();
  };
  // 0.9.0's one seam for running mcpp now runs it through the backend; the parsers are unchanged.
  useMcppExecutor(async (_executable, args, cwd, options = {}) => {
    try {
      const answer = await session.request<{ exitCode: number; stdout: string; stderr: string; timedOut: boolean }>("mcpp/exec", {
        args,
        ...(cwd ? { cwd } : {}),
        ...(options.timeoutMs ? { timeoutMs: options.timeoutMs } : {}),
        maxOutputBytes: Math.max(1, options.maxBufferMiB ?? 16) * 1024 * 1024,
      });
      return { exitCode: answer.timedOut && answer.exitCode === 0 ? 1 : answer.exitCode, stdout: answer.stdout, stderr: answer.stderr };
    } catch (error) {
      return { exitCode: 1, stdout: "", stderr: error instanceof Error ? error.message : String(error) };
    }
  });
  useMcpplsExecutor(async (args, cwd, options = {}) => {
    try {
      const answer = await session.request<{ exitCode: number; stdout: string; stderr: string; timedOut: boolean }>("languageService/exec", {
        args,
        ...(cwd ? { cwd } : {}),
        ...(options.timeoutMs ? { timeoutMs: options.timeoutMs } : {}),
        maxOutputBytes: Math.max(1, options.maxBufferMiB ?? 16) * 1024 * 1024,
      });
      return { exitCode: answer.timedOut && answer.exitCode === 0 ? 1 : answer.exitCode, stdout: answer.stdout, stderr: answer.stderr };
    } catch (error) {
      return { exitCode: 1, stdout: "", stderr: error instanceof Error ? error.message : String(error) };
    }
  });
  extensionContext.subscriptions.push({ dispose: () => { useMcppExecutor(undefined); useMcpplsExecutor(undefined); } });
  extensionContext.subscriptions.push(
    session,
    jobs,
    new Debugging(session, ideProjects),
    new Testing(session, jobs, ideProjects),
    jobs.onDidChangeRunning.event((n) => void vscode.commands.executeCommand("setContext", "mcpp.jobRunning", n > 0)),
    vscode.tasks.onDidStartTask(() => void updateJobRunning()),
    vscode.tasks.onDidEndTask(() => void updateJobRunning()),
    vscode.commands.registerCommand(CLI_COMMANDS.debug, runGuarded(async () => {
      const project = findCurrentProject();
      const folder = project ? vscode.workspace.getWorkspaceFolder(vscode.Uri.file(project.root)) : undefined;
      await vscode.debug.startDebugging(folder, { type: "mcpp", request: "launch", name: "mcpp: debug" });
    })),
    vscode.commands.registerCommand(CLI_COMMANDS.selectTarget, runGuarded(() => selectTarget(session, ideProjects))),
    vscode.commands.registerCommand(CLI_COMMANDS.showTargets, runGuarded(() => selectTarget(session, ideProjects, true))),
    vscode.commands.registerCommand(CLI_COMMANDS.pack, runGuarded(async () => {
      const project = findCurrentProject();
      if (project === undefined) {
        void vscode.window.showWarningMessage(t("No mcpp.toml was found in this workspace. Run this command inside an mcpp project."));
        return;
      }
      const args = await packArguments(session, project.root);
      if (args !== undefined) await cliController.runProjectTask("pack", { args });
    })),
    vscode.commands.registerCommand(CLI_COMMANDS.cancelJob, runGuarded(async () => {
      for (const execution of vscode.tasks.taskExecutions) {
        if (execution.task.definition.type === "mcpp") execution.terminate();
      }
      for (const id of jobs.runningIds) await session.request("job/cancel", { jobId: id });
    })),
    vscode.commands.registerCommand(CLI_COMMANDS.showCachePanel, () => vscode.commands.executeCommand(CACHE_VIEW_FOCUS_COMMAND)),
    // `mcpp.build.onSave`: a save in a project builds it, unless an mcpp task already runs.
    vscode.workspace.onDidSaveTextDocument(async (document) => {
      if (!read<boolean>("mcpp.build.onSave", document.uri) || cliController.isBusy()) return;
      if (!/\.(cppm|cpp|cc|cxx|ixx|mpp|h|hpp|toml|mcpp)$/.test(document.fileName)) return;
      const project = findCurrentProject();
      if (project && document.fileName.startsWith(project.root)) await cliController.runProjectTask("build", { notify: false });
    }),
  );
  function updateJobRunning(): Thenable<unknown> {
    const running = jobs.runningCount > 0 || vscode.tasks.taskExecutions.some((e) => e.task.definition.type === "mcpp");
    return vscode.commands.executeCommand("setContext", "mcpp.jobRunning", running);
  }
  void session.start();

  // ── views ────────────────────────────────────────────────────────────────
  // The C++ Modules block of the project view reads mcppls through the bridge; the views themselves
  // (the project tree, the libraries, the cache, the settings) are src/pages.ts's, which the
  // workbench mcpp-clion runs registers in the same way.
  const languageService = registerLanguageServerCommands(extensionContext, { bridge, output });
  registerPages(extensionContext, {
    output,
    mcppExecutable: (project) => cliController.mcppExecutable(project),
    languageService,
  });

  // Text analysis only: no mcpp process, so both stay available in a restricted
  // workspace, which is exactly what the `limited` capability promises.
  registerTomlProviders(
    extensionContext,
    () => read<boolean>("mcpp.toml.completion"),
    () => ({
      syntax: read("mcpp.toml.diagnostics.syntax"),
      unknownSection: read("mcpp.toml.diagnostics.unknownSection"),
      unknownKey: read("mcpp.toml.diagnostics.unknownKey"),
      planeSeparation: read("mcpp.toml.diagnostics.planeSeparation"),
      legacyKeys: read("mcpp.toml.diagnostics.legacyKeys"),
    }),
    () => read<boolean>("mcpp.toml.diagnostics.enabled"),
    () => read<boolean>("mcpp.toml.hover"),
    () => read<boolean>("mcpp.toml.navigation"),
  );
  registerBuildScriptProviders(extensionContext, () => {
    if (!read<boolean>("mcpp.buildScript.intelligence") || !read<boolean>("mcpp.buildScript.diagnostics")) {
      return "off";
    }
    return read<"warning" | "info" | "off">("mcpp.buildScript.diagnostics.severity");
  }, languageServerServes);

  extensionContext.subscriptions.push(
    output,
    manifestWatcher,
    inProjectContext,
    ...cliController.register(),

    // The ids from 0.4.x keep working.
    vscode.commands.registerCommand(CLI_COMMANDS.autoConfigureModules, runGuarded(async () => {
      const project = findCurrentProject();
      if (project === undefined) {
        await vscode.window.showWarningMessage(t("This workspace has no mcpp.toml."));
        return;
      }
      await autoConfigureModulesWizard(bridge, cliController, output);
    })),
    vscode.commands.registerCommand(LEGACY_LANGUAGE_SERVER_COMMANDS.refreshCompilationDatabase, runGuarded(async () => {
      await cliController.runProjectTask("build");
    })),

    // Environment self-check: one copyable snapshot of everything a bug report needs.
    vscode.commands.registerCommand(TOOL_COMMANDS.selfCheck, runGuarded(async () => {
      await showSelfCheck(output, cliController, bridge, extensionContext.extension.packageJSON.version);
    })),

    vscode.workspace.onDidGrantWorkspaceTrust(() => cliController.refreshStatus()),

    // The IDE report (§3.2): what a bug report needs, written where the person chooses.
    vscode.commands.registerCommand(TOOL_COMMANDS.exportReport, runGuarded(async () => {
      const project = findCurrentProject();
      let report: unknown;
      try {
        report = await session.request("report/collect", project ? { root: project.root } : {});
      } catch (error) {
        report = { kind: "mcpp-ide.report", version: 1, backend: null, error: String(error), degraded: session.degraded };
      }
      const stamp = new Date().toISOString().replace(/[:.]/g, "-");
      const target = await vscode.window.showSaveDialog({
        defaultUri: vscode.Uri.file(path.join(project?.root ?? os.homedir(), `mcpp-ide-report-${stamp}.json`)),
        filters: { JSON: ["json"] },
      });
      if (target === undefined) return;
      await vscode.workspace.fs.writeFile(target, Buffer.from(`${JSON.stringify(report, null, 2)}\n`, "utf8"));
      void vscode.window.showInformationMessage(t("The IDE report is written to {0}", target.fsPath));
    })),
    session.onDidStart.event(() => void refreshProjectMcpp()),
    session.onDidRefresh.event(() => { projectMcpp.clear(); void refreshProjectMcpp(); }),
    session.on("project/mcppChanged", (n: { root: string }) => { projectMcpp.delete(n.root); void refreshProjectMcpp(); }),
    vscode.window.onDidChangeActiveTextEditor(() => void refreshProjectMcpp()),
  );

  void offerSettingRenames(extensionContext);
  void noteUnverifiedLanguageService(output);

  if (read<boolean>("mcpp.diagnostics.selfCheckOnStartup")) {
    void showSelfCheck(output, cliController, bridge, extensionContext.extension.packageJSON.version);
  }

  extensionContext.subscriptions.push(
    manifestWatcher.onDidCreate(() => cliController.refreshStatus()),
    manifestWatcher.onDidChange(() => cliController.refreshStatus()),
    manifestWatcher.onDidDelete(() => cliController.refreshStatus()),
  );
  return { session };
}

/**
 * The 0.4.x keys that were renamed still hold values nothing reads. Offer to move
 * them, once per workspace — and leave the old key in place either way, because
 * deleting a user's setting is their decision.
 */
async function offerSettingRenames(context: vscode.ExtensionContext): Promise<void> {
  const pending = pendingRenames(vscode.window.activeTextEditor?.document.uri);
  if (pending.length === 0 || context.workspaceState.get<boolean>("mcpp.renamesOffered") === true) {
    return;
  }

  const toUser = t("Move them to my user settings");
  const toWorkspace = t("Move them to this workspace");
  const choice = await vscode.window.showInformationMessage(renamePrompt(pending), toUser, toWorkspace);
  if (choice !== toUser && choice !== toWorkspace) {
    // Dismissed is not answered: the marker is only written after a choice, so
    // the offer comes back next session instead of being lost forever — a
    // marker written before the prompt stranded exactly those users (external
    // review P2-9).
    return;
  }
  await context.workspaceState.update("mcpp.renamesOffered", true);
  const moved = await applyRenames(pending, choice === toUser ? "user" : "workspace");
  void vscode.window.showInformationMessage(t("mcpp: moved {0} setting(s).", moved));
}

/**
 * A version notice, never a gate. `extensionDependencies` cannot express a version
 * range, so a user can legitimately end up with a build of the language service
 * older than the one this extension was tested against; the capability probe means
 * that still works, but it is worth one sentence in the log.
 */
async function noteUnverifiedLanguageService(output: vscode.OutputChannel): Promise<void> {
  const version = languageServerVersion();
  if (version === undefined) {
    return;
  }
  // The same comparator the version matrix uses, against the contract's own
  // minimum — the old hand-rolled comparison had dead `< 0` branches and a
  // second, drifting copy of the floor (external review P1-3).
  const below = compareVersions(version, VERIFIED_MCPPLS_MINIMUM) < 0;
  if (below) {
    output.appendLine(
      t("{0} {1} is older than the verified range ({2}); the capability probe will hide what it cannot do.", MCPPLS_EXTENSION_ID, version, VERIFIED_MCPPLS_RANGE),
    );
  }
}

/**
 * The self-check gathers, never guesses: anything it cannot read is reported as
 * unknown rather than left blank. It is the first thing to ask for in a bug
 * report, and it writes to the `mcpp` channel so it can be copied in one go.
 */
async function showSelfCheck(
  output: vscode.OutputChannel,
  cliController: McppCliController,
  bridge: LanguageServerBridge,
  extensionVersion: unknown,
): Promise<void> {
  const project = findCurrentProject();
  const executable = cliController.mcppExecutable(project);
  const timeoutSeconds = read<number>("mcpp.runtime.timeoutSeconds");
  // The untrusted-workspace promise in the manifest is "no mcpp command runs",
  // and `mcpp.path` is resource-scoped, so the probe refuses rather than
  // executing a workspace-named program; the report says so in plain words.
  const probeResult = await runMcpp(vscode.workspace.isTrusted, executable, ["--protocol-version"], project?.root, {
    timeoutMs: timeoutSeconds > 0 ? timeoutSeconds * 1000 : undefined,
  });
  const info = probeResult !== undefined && probeResult.exitCode === 0 ? parseProtocolInfo(probeResult.stdout) : undefined;
  const state = readLanguageServerState();
  const capabilities = CAPABILITIES.map((entry) => ({
    key: entry.key,
    state: bridge.isGone(entry.key) ? "missing" : bridge.isUnconfirmed(entry.key) ? "unconfirmed" : "available",
  }));
  const text = buildSelfCheckText({
    extensionVersion: typeof extensionVersion === "string" ? extensionVersion : "unknown",
    vscodeVersion: vscode.version,
    platform: `${process.platform}-${process.arch}`,
    languagePreference: languagePreference(),
    trusted: vscode.workspace.isTrusted,
    workspaceRoots: (vscode.workspace.workspaceFolders ?? []).map((folder) => folder.uri.fsPath),
    projectRoot: project?.root,
    mcppPath: executable,
    mcppProbe:
      info === undefined
        ? undefined
        : { version: info.mcppVersion, envelopeMax: info.envelopeMax, kinds: Object.keys(info.kinds) },
    mcppls: {
      installed: languageServerInstalled(),
      version: languageServerVersion(),
      enabled: languageServerEnabled(),
      state: describeState(state),
      capabilities,
    },
    changedSettings: changedSettings(project === undefined ? undefined : vscode.Uri.file(project.root)),
    lastRefresh: lastRefresh,
    cache: await readCacheSnapshot(),
  });
  output.appendLine("");
  output.appendLine("===== mcpp: environment self-check =====");
  output.appendLine(text);
  if (probeResult === undefined) {
    output.appendLine(t("The workspace is not trusted; mcpp itself was not probed."));
  }
  output.appendLine("========================================");
  output.show(true);
}

export function deactivate(): void {
  // VS Code disposes everything registered in activate().
}
