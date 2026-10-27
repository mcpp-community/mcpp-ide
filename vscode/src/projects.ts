// Which project the user is in: the one the active editor's file belongs to (the backend answers,
// `projects/forFile`), else the first project of the window. The answer sets `mcpp.inProject` and
// `mcpp.editorTitleButtons`, which the generated menus and keybindings read.
import * as vscode from "vscode";

import type { Project } from "./protocol";
import type { Session } from "./session";

export class Projects implements vscode.Disposable {
  private current: Project | undefined;
  private readonly disposables: vscode.Disposable[] = [];
  readonly onDidChange = new vscode.EventEmitter<Project | undefined>();

  constructor(private readonly session: Session) {
    this.disposables.push(
      this.onDidChange,
      vscode.window.onDidChangeActiveTextEditor(() => void this.refresh()),
      vscode.workspace.onDidChangeWorkspaceFolders(() => void this.refresh()),
      vscode.workspace.onDidChangeConfiguration((e) => { if (e.affectsConfiguration("mcpp.task.editorTitleButtons")) void this.refresh(); }),
      session.onDidStart.event(() => void this.refresh()),
    );
  }

  get project(): Project | undefined {
    return this.current;
  }

  get all(): Project[] {
    return this.session.info?.projects ?? [];
  }

  /** The current project, asking the backend if the answer is not known yet. */
  async resolve(): Promise<Project | undefined> {
    if (!this.current) await this.refresh();
    return this.current;
  }

  async refresh(): Promise<void> {
    let next: Project | undefined;
    const editor = vscode.window.activeTextEditor;
    try {
      if (editor && editor.document.uri.scheme === "file") {
        const folder = vscode.workspace.getWorkspaceFolder(editor.document.uri);
        const boundary = vscode.workspace.getConfiguration().get<string>("mcpp.project.discoveryBoundary") === "filesystem" ? "" : folder?.uri.fsPath ?? "";
        next = (await this.session.request<Project | null>("projects/forFile", { path: editor.document.uri.fsPath, boundary })) ?? undefined;
      }
    } catch {
      next = undefined;
    }
    if (!next) next = this.current && this.all.some((p) => p.root === this.current!.root) ? this.current : this.all[0];
    const changed = next?.root !== this.current?.root;
    this.current = next;
    await vscode.commands.executeCommand("setContext", "mcpp.inProject", !!next);
    await vscode.commands.executeCommand("setContext", "mcpp.editorTitleButtons",
      vscode.workspace.getConfiguration().get<boolean>("mcpp.task.editorTitleButtons") ?? true);
    if (changed) this.onDidChange.fire(next);
  }

  /** The current project, or a message that there is none. */
  async require(): Promise<Project | undefined> {
    const p = await this.resolve();
    if (!p) void vscode.window.showWarningMessage(this.session.t("No mcpp.toml was found in this workspace. Run this command inside an mcpp project."));
    return p;
  }

  dispose(): void {
    this.disposables.forEach((d) => d.dispose());
  }
}
