/**
 * Run and Debug above `main` (design 2026-10-30 part 2 §6.1, §6.3): the backend says what the file
 * is to its project (`source/entries`: a program's `main`, a test's, or neither); the position is
 * the language service's, from the document symbols of the server answering the file (C++ Modules),
 * and only without one the line the backend read from the text, which the lens's tooltip calls
 * approximate. `mcpp.codeLens.enable` turns the lenses off.
 *
 * A program's Run and Debug are mcpp.run and mcpp.debug. A test's Run is its program in the Testing
 * view; its Debug is offered where the project's mcpp describes a test's launch (`test-launch`).
 *
 * THE ENTRY HAS AN ICON (design 2026-11-07, D16): the lenses are `$(play) Run` and `$(debug-alt) Debug`,
 * and while the active editor is a target's entry the editor title's Run menu (`editor/title/run`)
 * offers its Run and Debug (`mcpp.runThisFile`, `mcpp.debugThisFile`), by the context key
 * `mcpp.activeEntry` ("program", "test" or "") and `mcpp.activeEntryDebug`. A test's cases have the
 * Testing view's gutter icons, each with its last result.
 */
import * as vscode from "vscode";

import { CLI_COMMANDS } from "../commands/ids";
import { read } from "../config/access";
import { t } from "../i18n/t";
import { mainLineOf } from "./mapping";
import type { ProjectMcpp, SourceEntries } from "./protocol";
import type { Session } from "./session";
import type { Testing } from "./testing";

export const CODELENS_COMMANDS = {
  runTest: "mcpp.codeLens.runTest",
  debugTest: "mcpp.codeLens.debugTest",
  runThisFile: CLI_COMMANDS.runThisFile,
  debugThisFile: CLI_COMMANDS.debugThisFile,
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
      // The editor title's Run menu: the active editor's entry, run or debugged as its lens would.
      vscode.commands.registerCommand(CODELENS_COMMANDS.runThisFile, async () => {
        const entry = await this.activeEntry();
        if (entry?.kind === "program") await vscode.commands.executeCommand("mcpp.run");
        else if (entry?.kind === "test" && entry.root) await vscode.commands.executeCommand(CODELENS_COMMANDS.runTest, entry.root, entry.test ?? "");
      }),
      vscode.commands.registerCommand(CODELENS_COMMANDS.debugThisFile, async () => {
        const entry = await this.activeEntry();
        if (entry?.kind === "program") await vscode.commands.executeCommand("mcpp.debug");
        else if (entry?.kind === "test" && entry.root) await vscode.commands.executeCommand(CODELENS_COMMANDS.debugTest, entry.root, entry.test ?? "");
      }),
      vscode.window.onDidChangeActiveTextEditor(() => void this.updateContext()),
      vscode.workspace.onDidSaveTextDocument((doc) => { if (doc === vscode.window.activeTextEditor?.document) void this.updateContext(); }),
      session.onDidStart.event(() => void this.updateContext()),
    );
    void this.updateContext();
  }

  /** What the active editor's file is to its project, or undefined. */
  private async activeEntry(): Promise<SourceEntries | undefined> {
    const document = vscode.window.activeTextEditor?.document;
    if (!document || document.languageId !== "cpp" || document.uri.scheme !== "file" || !this.session.running) return undefined;
    try {
      return await this.session.request<SourceEntries>("source/entries", { file: document.uri.fsPath, text: document.getText() });
    } catch {
      return undefined;
    }
  }

  /** `mcpp.activeEntry` and `mcpp.activeEntryDebug`, for the editor title's Run menu. */
  private async updateContext(): Promise<void> {
    const entry = await this.activeEntry();
    const kind = entry && entry.kind !== "none" && entry.root !== undefined ? entry.kind : "";
    const debug = kind === "program" || (kind === "test" && !!entry?.root && !!this.projectMcpp(entry.root)?.features.includes("test-launch"));
    await vscode.commands.executeCommand("setContext", "mcpp.activeEntry", kind);
    await vscode.commands.executeCommand("setContext", "mcpp.activeEntryDebug", debug);
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
    const run = `$(play) ${t("Run")}`;
    const debug = `$(debug-alt) ${t("Debug")}`;
    if (entries.kind === "program")
      return [
        new vscode.CodeLens(range, { title: run, command: "mcpp.run", tooltip }),
        new vscode.CodeLens(range, { title: debug, command: "mcpp.debug", tooltip }),
      ];
    const test = entries.test ?? "";
    const lenses = [new vscode.CodeLens(range, { title: run, command: CODELENS_COMMANDS.runTest, arguments: [root, test], tooltip })];
    if (this.projectMcpp(root)?.features.includes("test-launch"))
      lenses.push(new vscode.CodeLens(range, { title: debug, command: CODELENS_COMMANDS.debugTest, arguments: [root, test], tooltip }));
    return lenses;
  }

  dispose(): void {
    this.disposables.forEach((d) => d.dispose());
  }
}
