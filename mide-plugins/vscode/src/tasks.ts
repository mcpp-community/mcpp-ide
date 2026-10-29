// mcpp's builds and tests as VS Code tasks (type "mcpp"): each runs as a backend job in a
// pseudoterminal that shows mcpp's own narration, in colour, and ends with the job's exit code.
// Closing the terminal cancels the job; mcpp then ends the steps it started and records the build as
// cancelled. A task's problems come from mcpp's diagnostics (diagnostics.ts), not from a matcher.
import * as vscode from "vscode";

import type { Jobs, JobHandle } from "./jobs";
import type { Projects } from "./projects";
import type { Session } from "./session";

export interface McppTaskDefinition extends vscode.TaskDefinition {
  type: "mcpp";
  command: "build" | "test" | "run" | "clean";
  target?: string;
  profile?: "dev" | "release";
  args?: string[];
  root?: string;
}

/** A terminal that renders one backend job. */
export class JobTerminal implements vscode.Pseudoterminal {
  private readonly writeEmitter = new vscode.EventEmitter<string>();
  private readonly closeEmitter = new vscode.EventEmitter<number>();
  readonly onDidWrite = this.writeEmitter.event;
  readonly onDidClose = this.closeEmitter.event;
  private handle: JobHandle | undefined;
  private ended = false;

  constructor(private readonly start: (term: JobTerminal) => Promise<JobHandle | undefined>, private readonly session: Session) {}

  print(text: string): void {
    this.writeEmitter.fire(text.replace(/\r?\n/g, "\r\n"));
  }

  end(code: number): void {
    if (this.ended) return;
    this.ended = true;
    this.closeEmitter.fire(code);
  }

  open(): void {
    if (vscode.workspace.getConfiguration().get<boolean>("mcpp.task.clearTerminal")) this.writeEmitter.fire("\x1bc");
    this.start(this).then((h) => { this.handle = h; if (!h) this.end(1); }, (e) => {
      this.print(`\x1b[31m${this.session.t("mcpp: {0}", e instanceof Error ? e.message : String(e))}\x1b[0m\n`);
      this.end(1);
    });
  }

  close(): void {
    // The terminal closed while the job runs: ask mcpp to end it.
    if (!this.ended && this.handle) void this.handle.cancel();
  }
}

export class Tasks implements vscode.TaskProvider, vscode.Disposable {
  private readonly registration: vscode.Disposable;

  constructor(private readonly session: Session, private readonly jobs: Jobs, private readonly projects: Projects) {
    this.registration = vscode.tasks.registerTaskProvider("mcpp", this);
  }

  provideTasks(): vscode.Task[] {
    const root = this.projects.project?.root;
    if (!root) return [];
    return (["build", "test"] as const).map((command) => this.task({ type: "mcpp", command }, root));
  }

  resolveTask(task: vscode.Task): vscode.Task | undefined {
    const def = task.definition as McppTaskDefinition;
    return this.task(def, def.root || this.projects.project?.root || "", task.scope);
  }

  task(def: McppTaskDefinition, root: string, scope?: vscode.WorkspaceFolder | vscode.TaskScope): vscode.Task {
    const folder = scope ?? vscode.workspace.getWorkspaceFolder(vscode.Uri.file(root)) ?? vscode.TaskScope.Workspace;
    const execution = new vscode.CustomExecution(async () => new JobTerminal((term) => this.run(def, root, term), this.session));
    const task = new vscode.Task(def, folder, def.command, "mcpp", execution, []);
    task.group = def.command === "build" ? vscode.TaskGroup.Build : def.command === "test" ? vscode.TaskGroup.Test : undefined;
    const reveal = vscode.workspace.getConfiguration().get<string>("mcpp.task.revealTerminal");
    task.presentationOptions = {
      reveal: reveal === "never" ? vscode.TaskRevealKind.Never : reveal === "silent" ? vscode.TaskRevealKind.Silent : vscode.TaskRevealKind.Always,
      focus: vscode.workspace.getConfiguration().get<boolean>("mcpp.task.focusTerminal") ?? false,
      clear: false,
    };
    return task;
  }

  private async run(def: McppTaskDefinition, root: string, term: JobTerminal): Promise<JobHandle | undefined> {
    if (!root) { term.print(`${this.session.t("No mcpp project in this workspace")}\n`); return undefined; }
    const params: Record<string, unknown> = { kind: def.command === "test" ? "test" : "build", root, color: true, supersede: true };
    if (def.target !== undefined) params.target = def.target;
    if (def.profile) params.profile = def.profile;
    if (def.args?.length) params.args = def.args;
    const handle = await this.jobs.start(params, {
      output: (text) => term.print(text),
      testResult: (r) => {
        const mark = r.status === "passed" ? "\x1b[32m✓\x1b[0m" : r.status === "skipped" ? "-" : "\x1b[31m✗\x1b[0m";
        term.print(`${mark} ${r.test} (${r.durationMs} ms)\n`);
      },
      finished: (p) => {
        term.print(`\n${p.status === "ok" ? "\x1b[32m" : "\x1b[31m"}${this.session.t("mcpp {0}: {1}", def.command, p.status)}\x1b[0m\n`);
        term.end(p.exitCode ?? (p.status === "ok" ? 0 : 1));
      },
    });
    return handle;
  }

  /** Runs a build or test task now, as the commands do. */
  async execute(command: "build" | "test", root: string, extra: Partial<McppTaskDefinition> = {}): Promise<vscode.TaskExecution> {
    return vscode.tasks.executeTask(this.task({ type: "mcpp", command, ...extra }, root));
  }

  dispose(): void {
    this.registration.dispose();
  }
}
