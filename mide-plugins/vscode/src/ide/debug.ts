/**
 * Debugging: a launch configuration of type "mcpp" names what to debug (target, profile,
 * arguments); the backend builds the program and answers the configuration of the debugger at hand
 * (CodeLLDB, the C/C++ extension, lldb-dap or Native Debug), which this provider returns in its
 * place. A program that runs through a runner (an ok image under okrun) is debugged as the runner.
 */
import * as vscode from "vscode";

import { t } from "../i18n/t";
import { availableDebuggers } from "./mapping";
import type { IdeProjects } from "./projects";
import type { Jobs } from "./jobs";
import type { Session } from "./session";

export class Debugging implements vscode.DebugConfigurationProvider, vscode.Disposable {
  private readonly disposables: vscode.Disposable[] = [];

  constructor(private readonly session: Session, private readonly projects: IdeProjects,
    private readonly ensureMcpp: () => Promise<boolean>, private readonly jobs?: Jobs) {
    this.disposables.push(
      vscode.debug.registerDebugConfigurationProvider("mcpp", this),
      vscode.debug.registerDebugConfigurationProvider("mcpp", { provideDebugConfigurations: () => [{ type: "mcpp", request: "launch", name: "mcpp: debug" }] },
        vscode.DebugConfigurationProviderTriggerKind.Dynamic),
    );
  }

  async resolveDebugConfiguration(folder: vscode.WorkspaceFolder | undefined, config: vscode.DebugConfiguration): Promise<vscode.DebugConfiguration | undefined> {
    if (!config.type) Object.assign(config, { type: "mcpp", request: "launch", name: "mcpp: debug" });
    const project = (config.root && { root: config.root as string }) || this.projects.current();
    if (!project) {
      void vscode.window.showWarningMessage(t("No mcpp.toml was found in this workspace. Run this command inside an mcpp project."));
      return undefined;
    }
    if (!(await this.ensureMcpp())) return undefined;
    const available = availableDebuggers((id) => vscode.extensions.getExtension(id) !== undefined);
    if (available.length === 0) {
      const install = t("Install CodeLLDB");
      // The session is refused now; the message's action is the user's, whenever they take it.
      void vscode.window.showErrorMessage(
        t("No debugger extension is installed: install CodeLLDB, the C/C++ extension or lldb-dap."), install)
        .then((choice) => choice === install ? vscode.commands.executeCommand("workbench.extensions.search", "@id:vadimcn.vscode-lldb") : undefined);
      return undefined;
    }
    try {
      const answer = await vscode.window.withProgress(
        { location: vscode.ProgressLocation.Window, title: t("mcpp: building for the debugger…") },
        async (progress) => {
          // The build first, as a launch job (design 2026-11-07 §4.9): mcpp's narration in the output and
          // its steps on the progress; then the debugger's configuration, which mcpp describes on its fast path.
          if (this.jobs && !config.test) {
            let reported = 0;
            const job = await this.jobs.start({
              kind: "launch", root: project.root,
              ...(config.target !== undefined ? { target: config.target } : {}),
              ...(config.profile ? { profile: config.profile } : {}),
              args: Array.isArray(config.args) ? config.args : [], color: false,
            }, {
              output: (text) => this.session.output.append(text),
              progress: ({ finished, total }) => {
                const now = total > 0 ? Math.floor((finished / total) * 100) : 0;
                progress.report({ message: `${finished}/${total}`, increment: Math.max(0, now - reported) });
                reported = Math.max(reported, now);
              },
            });
            const built = await job.done;
            if (built.status !== "ok") throw new Error(t("the program could not be built"));
          }
          return this.session.request<{ engine: string; configuration: vscode.DebugConfiguration & { mcppNote?: string } }>("debug/configuration", {
          root: project.root,
          // A test's program (mcpp test --print-launch), where the project's mcpp describes one.
          ...(config.test ? { test: config.test } : {}),
          // Some of its cases (mcpp test spec P4), as the program lists them.
          ...(Array.isArray(config.cases) && config.cases.length > 0 ? { cases: config.cases } : {}),
          ...(config.target !== undefined ? { target: config.target } : {}),
          ...(config.profile ? { profile: config.profile } : {}),
          ...(config.engine ? { engine: config.engine } : {}),
          args: Array.isArray(config.args) ? config.args : [],
          available,
          platform: process.platform === "darwin" ? "macos" : process.platform === "win32" ? "windows" : "linux",
        });
        });
      const { mcppNote, ...resolved } = answer.configuration;
      if (mcppNote) this.session.output.appendLine(`mcpp: ${mcppNote}`);
      return Object.assign({}, resolved, { name: config.name || resolved.name });
    } catch (e) {
      const detail = (e as { error?: { data?: { narration?: string } } }).error?.data?.narration;
      if (detail) this.session.output.appendLine(detail);
      void vscode.window.showErrorMessage(t("mcpp: {0}", e instanceof Error ? e.message : String(e)));
      return undefined;
    }
  }

  /** `mcpp.debug`: the same, without a launch.json. */
  async start(): Promise<void> {
    const project = this.projects.current();
    const folder = project ? vscode.workspace.getWorkspaceFolder(vscode.Uri.file(project.root)) : undefined;
    await vscode.debug.startDebugging(folder, { type: "mcpp", request: "launch", name: "mcpp: debug" });
  }

  dispose(): void {
    this.disposables.forEach((d) => d.dispose());
  }
}
