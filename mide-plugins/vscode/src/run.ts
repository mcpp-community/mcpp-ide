// Running: the backend builds the program and answers how it starts (mcpp run --print-launch: argv,
// the environment mcpp adds, the working directory, the runner); the program then runs in a terminal
// of its own, where it reads input and its colours are its own.
import * as vscode from "vscode";

import type { Launch } from "./protocol";
import type { Session } from "./session";

export async function runProgram(session: Session, root: string, options: { target?: string; bin?: string } = {}): Promise<void> {
  const args = vscode.workspace.getConfiguration().get<string[]>("mcpp.task.runArgs") ?? [];
  let launch: Launch;
  try {
    launch = await vscode.window.withProgress(
      { location: vscode.ProgressLocation.Window, title: session.t("mcpp: building…") },
      () => session.request<Launch>("run/launch", { root, args, ...(options.target !== undefined ? { target: options.target } : {}), ...(options.bin ? { bin: options.bin } : {}) }));
  } catch (e) {
    const data = (e as { error?: { data?: { narration?: string } } }).error?.data;
    if (data?.narration) { session.output.appendLine(data.narration); session.output.show(true); }
    void vscode.window.showErrorMessage(session.t("mcpp: {0}", e instanceof Error ? e.message : String(e)));
    return;
  }
  const [program, ...rest] = launch.argv;
  const name = `mcpp run: ${(launch.program ?? program).split(/[\\/]/).pop()}`;
  vscode.window.terminals.find((t) => t.name === name)?.dispose();
  const terminal = vscode.window.createTerminal({ name, shellPath: program, shellArgs: rest, cwd: launch.cwd, env: launch.env });
  terminal.show(!(vscode.workspace.getConfiguration().get<boolean>("mcpp.task.focusTerminal") ?? true));
}
