/**
 * Run and Debug above `main` (design 2026-10-30 part 2 §6.1, §6.3): the backend says what the file
 * is to its project (`source/entries`: a program's `main`, a test's, or neither); the position is
 * the language service's, from the document symbols of the server answering the file (C++ Modules),
 * and only without one the line the backend read from the text, which the lens's tooltip calls
 * approximate. `mcpp.codeLens.enable` turns the lenses off.
 *
 * A program's Run and Debug are mcpp.run and mcpp.debug. A test's Run is its program in the Testing
 * view; its Debug is offered where the project's mcpp describes a test's launch (`test-launch`).
 */
import * as vscode from "vscode";

import { read } from "../config/access";
import { t } from "../i18n/t";
import { mainLineOf } from "./mapping";
import type { ProjectMcpp, SourceEntries } from "./protocol";
import type { Session } from "./session";
import type { Testing } from "./testing";

export const CODELENS_COMMANDS = {
  runTest: "mcpp.codeLens.runTest",
  debugTest: "mcpp.codeLens.debugTest",
} as const;

export class McppCodeLens implements vscode.CodeLensProvider, vscode.Disposable {
  private readonly changed = new vscode.EventEmitter<void>();
  readonly onDidChangeCodeLenses = this.changed.event;
  private readonly disposables: vscode.Disposable[] = [this.changed];

  constructor(
    private readonly session: Session,
    private readonly testing: Testing,
    private readonly projectMcpp: (root: string) => ProjectMcpp | undefined,
  ) {
    this.disposables.push(
      vscode.languages.registerCodeLensProvider([{ language: "cpp", scheme: "file" }], this),
      session.onDidStart.event(() => this.changed.fire()),
      vscode.workspace.onDidChangeConfiguration((e) => { if (e.affectsConfiguration("mcpp.codeLens")) this.changed.fire(); }),
      vscode.commands.registerCommand(CODELENS_COMMANDS.runTest, async (root: string, test: string) => {
        if (!this.testing.item(root, test)) await this.testing.discover();
        const item = this.testing.item(root, test);
        if (item) await this.testing.runItem(item);
      }),
      vscode.commands.registerCommand(CODELENS_COMMANDS.debugTest, async (root: string, test: string) => {
        const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(root));
        await vscode.debug.startDebugging(folder, { type: "mcpp", request: "launch", name: `mcpp: debug ${test}`, root, test });
      }),
    );
  }

  async provideCodeLenses(document: vscode.TextDocument, token: vscode.CancellationToken): Promise<vscode.CodeLens[]> {
    if (read<boolean>("mcpp.codeLens.enable") === false || !this.session.running) return [];
    let entries: SourceEntries;
    try {
      entries = await this.session.request<SourceEntries>("source/entries", { file: document.uri.fsPath, text: document.getText() });
    } catch {
      return [];
    }
    if (token.isCancellationRequested || entries.kind === "none" || entries.root === undefined) return [];
    let line: number | undefined;
    try {
      const symbols = await vscode.commands.executeCommand<vscode.DocumentSymbol[] | undefined>("vscode.executeDocumentSymbolProvider", document.uri);
      line = mainLineOf(symbols);
    } catch {
      line = undefined;
    }
    const approximate = line === undefined;
    line ??= entries.main?.line;
    if (line === undefined) return [];
    const range = new vscode.Range(line, 0, line, 0);
    const tooltip = approximate ? t("The line of main was read from the text; C++ Modules gives its position when it serves this file.") : undefined;
    const root = entries.root;
    if (entries.kind === "program")
      return [
        new vscode.CodeLens(range, { title: t("Run"), command: "mcpp.run", tooltip }),
        new vscode.CodeLens(range, { title: t("Debug"), command: "mcpp.debug", tooltip }),
      ];
    const test = entries.test ?? "";
    const lenses = [new vscode.CodeLens(range, { title: t("Run"), command: CODELENS_COMMANDS.runTest, arguments: [root, test], tooltip })];
    if (this.projectMcpp(root)?.features.includes("test-launch"))
      lenses.push(new vscode.CodeLens(range, { title: t("Debug"), command: CODELENS_COMMANDS.debugTest, arguments: [root, test], tooltip }));
    return lenses;
  }

  dispose(): void {
    this.disposables.forEach((d) => d.dispose());
  }
}
