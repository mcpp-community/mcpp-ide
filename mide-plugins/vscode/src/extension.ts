// mcpp for VS Code: the adapter of mcpp-ide. It starts the backend (`mcpp-ide serve`) and maps its
// protocol onto VS Code: tasks in pseudoterminals, the Testing view, debug configurations, the
// Problems panel, the project view, the shared panels, mcpp.toml editing help. Language features of
// C++ files are mcppls's (the extension pack installs it); this extension registers none.
import * as vscode from "vscode";

import { dispatch, registerCommands, type CommandContext } from "./commands";
import { Debugging } from "./debug";
import { Problems } from "./diagnostics";
import { Jobs } from "./jobs";
import { Panels } from "./panels";
import { ProjectTree } from "./projectTree";
import { Projects } from "./projects";
import { Session } from "./session";
import { StatusBar } from "./status";
import { Tasks } from "./tasks";
import { Testing } from "./testing";
import { registerToml } from "./toml";

export interface McppApi {
  session: Session;
  projects: Projects;
}

export async function activate(context: vscode.ExtensionContext): Promise<McppApi> {
  const output = vscode.window.createOutputChannel("mcpp");
  const session = new Session(context, output);
  const projects = new Projects(session);
  const jobs = new Jobs(session);
  const tasks = new Tasks(session, jobs, projects);
  const debugging = new Debugging(session, projects);
  // The panels run commands, and the commands open panels: the context is completed once both exist.
  const c = { session, projects, jobs, tasks, debugging } as CommandContext;
  c.panels = new Panels(context, session, async (command, params) => {
    const entry = session.commands.find((x) => x.id === command);
    if (!entry) throw new Error(session.t("unknown command {0}", command));
    await dispatch(c, entry, [params?.args ?? params]);
    return true;
  });
  const panels = c.panels;
  context.subscriptions.push(output, session, projects, jobs, tasks, debugging, panels,
    new Problems(session), new Testing(session, jobs, projects), new ProjectTree(session, projects, jobs), new StatusBar(session, projects, jobs));
  registerCommands(context, c);
  registerToml(context, session);

  const views = () => {
    const get = (k: string) => vscode.workspace.getConfiguration().get<boolean>(k) ?? true;
    void vscode.commands.executeCommand("setContext", "mcpp.sidebarHidden", !get("mcpp.views.enabled"));
    void vscode.commands.executeCommand("setContext", "mcpp.views.project", get("mcpp.views.project.show"));
    void vscode.commands.executeCommand("setContext", "mcpp.views.library", get("mcpp.views.library.show"));
    void vscode.commands.executeCommand("setContext", "mcpp.views.cache", get("mcpp.views.cache.show"));
  };
  views();
  context.subscriptions.push(
    vscode.workspace.onDidChangeConfiguration((e) => { if (e.affectsConfiguration("mcpp.views")) views(); }),
    jobs.onDidChangeRunning.event((n) => void vscode.commands.executeCommand("setContext", "mcpp.jobRunning", n > 0)),
    // A save builds the file's project when asked to (mcpp.build.onSave); a save while a build
    // waits replaces it, and the running one is superseded.
    vscode.workspace.onDidSaveTextDocument(async (doc) => {
      if (!(vscode.workspace.getConfiguration().get<boolean>("mcpp.build.onSave") ?? false)) return;
      if (!/\.(cppm|cpp|cc|cxx|ixx|mpp|h|hpp|toml|mcpp)$/.test(doc.fileName)) return;
      const project = await projects.resolve();
      if (project && doc.fileName.startsWith(project.root)) await tasks.execute("build", project.root);
    }),
  );

  if (await session.start()) {
    if (vscode.workspace.getConfiguration().get<boolean>("mcpp.diagnostics.selfCheckOnStartup"))
      void vscode.commands.executeCommand("mcpp.selfCheck");
    // The language service is not what it should be: said once, with what to do; activation does not
    // wait for an answer.
    const ls = session.info?.languageService;
    if (ls && ls.state !== "ok")
      void vscode.window.showWarningMessage(`mcpp: ${ls.message}`, ...ls.actions.map((a) => a.title)).then((choice) => {
        const action = ls.actions.find((a) => a.title === choice);
        if (action) void vscode.commands.executeCommand("mcpp.languageServer.act", action.id);
      });
  }
  return { session, projects };
}

export function deactivate(): void {
  // Everything registered in activate() is disposed by VS Code; the session stops the backend.
}
