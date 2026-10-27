// The project view (mcpp.project): the current project's executables, tests and libraries, the
// target it builds for and the platform that configures it, its last build, and the language
// service that answers its sources. Everything shown is the backend's `project/summary` and
// `languageService/status`.
import * as vscode from "vscode";

import type { Jobs } from "./jobs";
import type { Projects } from "./projects";
import type { LanguageServiceStatus, ProjectSummary } from "./protocol";
import type { Session } from "./session";

class Node extends vscode.TreeItem {
  children?: Node[];
  constructor(label: string, opts: { description?: string; icon?: string; context?: string; command?: vscode.Command; tooltip?: string; children?: Node[]; expanded?: boolean } = {}) {
    super(label, opts.children ? (opts.expanded === false ? vscode.TreeItemCollapsibleState.Collapsed : vscode.TreeItemCollapsibleState.Expanded) : vscode.TreeItemCollapsibleState.None);
    this.description = opts.description;
    if (opts.icon) this.iconPath = new vscode.ThemeIcon(opts.icon);
    this.contextValue = opts.context;
    this.command = opts.command;
    this.tooltip = opts.tooltip;
    this.children = opts.children;
  }
}

export class ProjectTree implements vscode.TreeDataProvider<Node>, vscode.Disposable {
  private readonly changed = new vscode.EventEmitter<void>();
  readonly onDidChangeTreeData = this.changed.event;
  private readonly disposables: vscode.Disposable[] = [];
  private summary: ProjectSummary | undefined;
  private languageService: LanguageServiceStatus | undefined;
  private error: string | undefined;

  constructor(private readonly session: Session, private readonly projects: Projects, private readonly jobs: Jobs) {
    const refresh = () => void this.load();
    this.disposables.push(
      this.changed,
      vscode.window.registerTreeDataProvider("mcpp.project", this),
      projects.onDidChange.event(refresh),
      session.on("result/changed", refresh),
      session.on("manifest/changed", refresh),
      session.on("job/plan", refresh),
      jobs.onDidChangeRunning.event(() => this.changed.fire()),
      vscode.workspace.onDidChangeConfiguration((e) => { if (e.affectsConfiguration("mcpp.build.target")) refresh(); }),
      vscode.extensions.onDidChange(refresh),
    );
  }

  async load(): Promise<void> {
    const project = this.projects.project;
    this.error = undefined;
    if (!project) { this.summary = undefined; this.changed.fire(); return; }
    try {
      [this.summary, this.languageService] = await Promise.all([
        this.session.request<ProjectSummary>("project/summary", { root: project.root }),
        this.session.request<LanguageServiceStatus>("languageService/status", { candidates: this.session.mcpplsCandidates() }),
      ]);
    } catch (e) {
      this.error = e instanceof Error ? e.message : String(e);
    }
    this.changed.fire();
  }

  getTreeItem(n: Node): vscode.TreeItem {
    return n;
  }

  getChildren(n?: Node): Node[] {
    if (n) return n.children ?? [];
    const t = (k: string, ...a: unknown[]) => this.session.t(k, ...a);
    const project = this.projects.project;
    if (!project) return [new Node(t("No mcpp project in this workspace"), { icon: "info" })];
    if (this.error) return [new Node(t("mcpp could not answer"), { icon: "error", tooltip: this.error, command: { command: "mcpp.selfCheck", title: "" } })];
    const s = this.summary;
    if (!s) return [new Node(t("Loading…"), { icon: "loading~spin" })];
    const cmd = (command: string, ...args: unknown[]): vscode.Command => ({ command, title: "", arguments: args });
    const out: Node[] = [];
    out.push(new Node(project.name, {
      description: s.toolchain, icon: "project", tooltip: project.root,
      command: cmd("mcpp.openProjectPanel"),
    }));
    out.push(new Node(t("Target"), {
      description: `${s.target || t("this machine")}${s.platformText ? ` · ${s.platformText}` : ""}`, icon: "target",
      command: cmd("mcpp.selectTarget"), tooltip: s.hostPlatformText ? `${t("Host platform")}: ${s.hostPlatformText}` : undefined,
    }));
    if (s.lastBuild) {
      const ok = s.lastBuild.status === "ok";
      out.push(new Node(t("Last build"), { description: `${s.lastBuild.status}${s.lastBuild.client ? ` · ${s.lastBuild.client}` : ""}`, icon: ok ? "pass" : "error" }));
    }
    if (this.jobs.runningCount > 0)
      out.push(new Node(t("{0} job(s) running", this.jobs.runningCount), { icon: "sync~spin", command: cmd("mcpp.cancelJob"), tooltip: t("Cancel the Running Job") }));
    out.push(new Node(t("Executables"), { icon: "symbol-event", children: s.targets.map((x) =>
      new Node(x.name, { icon: "play", context: "mcppExecutable", command: cmd("mcpp.run", { bin: x.name }) })) }));
    out.push(new Node(t("Tests"), { icon: "beaker", children: s.tests.map((x) =>
      new Node(x.name, { icon: "beaker", context: "mcppTest", command: cmd("mcpp.test", { filter: x.name }) })) }));
    out.push(new Node(t("Libraries"), { icon: "library", expanded: false, children: s.libraries.map((x) => new Node(x.name, { description: x.kind, icon: "package" })) }));
    out.push(new Node(t("Dependencies"), { icon: "type-hierarchy", context: "mcppProjectDependencies", command: cmd("mcpp.project.showDependencyGraph") }));
    const ls = this.languageService;
    if (ls) {
      const state = ls.state === "ok" ? "pass" : ls.state === "missing" ? "error" : "warning";
      out.push(new Node(t("C++ Modules language service"), {
        description: ls.state === "ok" ? `mcppls ${ls.version}` : t("needs attention"), icon: state, tooltip: ls.message,
        children: [
          new Node(ls.message, { icon: "info", tooltip: ls.message }),
          ...ls.actions.map((a) => new Node(a.title, { icon: "cloud-download", command: cmd("mcpp.languageServer.act", a.id) })),
          new Node(t("Restart the language server"), { icon: "debug-restart", command: cmd("mcpp.languageServer.restart") }),
          new Node(t("Open the log"), { icon: "output", command: cmd("mcpp.languageServer.showLogs") }),
        ],
        expanded: ls.state !== "ok",
      }));
    }
    return out;
  }

  dispose(): void {
    this.disposables.forEach((d) => d.dispose());
  }
}
