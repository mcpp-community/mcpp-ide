// The status bar: the target the project builds for (and its platform), and whether a job runs.
// A click on the first selects another target; on the second, cancels the running jobs.
import * as vscode from "vscode";

import type { Jobs } from "./jobs";
import type { Projects } from "./projects";
import type { Session } from "./session";

export class StatusBar implements vscode.Disposable {
  private readonly target = vscode.window.createStatusBarItem("mcpp.target", vscode.StatusBarAlignment.Left, 50);
  private readonly job = vscode.window.createStatusBarItem("mcpp.job", vscode.StatusBarAlignment.Left, 49);
  private readonly disposables: vscode.Disposable[] = [this.target, this.job];
  private platformText = "";

  constructor(private readonly session: Session, private readonly projects: Projects, private readonly jobs: Jobs) {
    this.target.name = "mcpp target";
    this.target.command = "mcpp.selectTarget";
    this.job.name = "mcpp job";
    this.job.command = "mcpp.cancelJob";
    this.disposables.push(
      projects.onDidChange.event(() => this.update()),
      jobs.onDidChangeRunning.event(() => this.update()),
      session.on("job/plan", (p: { root: string; platformText?: string }) => {
        if (p.root === projects.project?.root) { this.platformText = p.platformText ?? ""; this.update(); }
      }),
      vscode.workspace.onDidChangeConfiguration((e) => { if (e.affectsConfiguration("mcpp.build.target") || e.affectsConfiguration("mcpp.ui.statusBar.show")) this.update(); }),
    );
    this.update();
  }

  private update(): void {
    const t = (k: string, ...a: unknown[]) => this.session.t(k, ...a);
    const show = vscode.workspace.getConfiguration().get<boolean>("mcpp.ui.statusBar.show") ?? true;
    if (!show || !this.projects.project) { this.target.hide(); this.job.hide(); return; }
    const target = vscode.workspace.getConfiguration().get<string>("mcpp.build.target") || t("this machine");
    this.target.text = `$(target) ${target}`;
    this.target.tooltip = [t("The target build, run, test, debug and pack use"), this.platformText].filter(Boolean).join("\n");
    this.target.show();
    if (this.jobs.runningCount > 0) {
      this.job.text = `$(sync~spin) mcpp`;
      this.job.tooltip = t("{0} job(s) running", this.jobs.runningCount);
      this.job.show();
    } else this.job.hide();
  }

  dispose(): void {
    this.disposables.forEach((d) => d.dispose());
  }
}
