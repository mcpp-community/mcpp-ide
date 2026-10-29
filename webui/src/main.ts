/**
 * The workbench's start: the IDE says where it is (its system, language, folders, trust, theme),
 * mcpp and mcppls are routed through the backend as the extension routes them, and the pages are
 * registered by mcpp-vscode's own `registerPages` -- the same modules, wired the same way.
 */
import manifest from "../../mide-plugins/vscode/package.json";
import { useMcppExecutor, useMcpplsExecutor } from "../../mide-plugins/vscode/src/cli/process";
import { setLanguagePreference, type LanguagePreference } from "../../mide-plugins/vscode/src/i18n/t";
import { read } from "../../mide-plugins/vscode/src/config/access";
import { onDidChange } from "../../mide-plugins/vscode/src/config/access";
import { registerPages } from "../../mide-plugins/vscode/src/pages";
import { on, request, rpc, setInfo, type HostInfo } from "./bridge";
import { configure as configurePath } from "./node/path";
import { Uri, commands, loadSettings, window } from "./vscode";
import { workbench } from "./workbench";

interface Init {
  info: HostInfo;
  theme: Record<string, string>;
  themeKind: "vscode-light" | "vscode-dark" | "vscode-high-contrast";
  /** `window`: the views as tabs; `settings`: the settings page alone (the IDE's settings dialog). */
  mode?: "window" | "settings";
}

interface Exec { exitCode: number; stdout: string; stderr: string; timedOut: boolean }

async function start(): Promise<void> {
  const init = await request<Init>("init");
  setInfo(init.info);
  configurePath();
  workbench.setTheme(init.theme, init.themeKind);
  on("theme", (t: { theme: Record<string, string>; themeKind: Init["themeKind"] }) => workbench.setTheme(t.theme, t.themeKind));
  await loadSettings();
  const language = () => setLanguagePreference(read<LanguagePreference>("mcpp.ui.language"));
  language();
  onDidChange(language);

  const exec = (method: string) => async (args: string[], cwd?: string, options: { timeoutMs?: number; maxBufferMiB?: number } = {}) => {
    try {
      const answer = await rpc<Exec>(method, {
        args, ...(cwd ? { cwd } : {}), ...(options.timeoutMs ? { timeoutMs: options.timeoutMs } : {}),
        maxOutputBytes: Math.max(1, options.maxBufferMiB ?? 16) * 1024 * 1024,
      });
      return { exitCode: answer.timedOut && answer.exitCode === 0 ? 1 : answer.exitCode, stdout: answer.stdout, stderr: answer.stderr };
    } catch (error) {
      return { exitCode: 1, stdout: "", stderr: error instanceof Error ? error.message : String(error) };
    }
  };
  useMcppExecutor((_executable, args, cwd, options) => exec("mcpp/exec")(args, cwd, options));
  useMcpplsExecutor(exec("languageService/exec"));

  const context = {
    subscriptions: [] as Array<{ dispose(): unknown }>,
    extensionUri: Uri.parse(init.info.extensionUri),
    extension: { packageJSON: manifest },
  };
  registerPages(context as never, {
    output: window.createOutputChannel("mcpp"),
    // The backend runs the mcpp it located; the name is only what a message would say.
    mcppExecutable: () => "mcpp",
  });
  if (init.mode === "settings") {
    document.body.classList.add("settings-only");
    void commands.executeCommand("mcpp.openSettings");
  } else {
    workbench.showFirst();
  }
  on("show", (p: { view: string; args?: unknown }) => {
    // A panel the IDE asks for: the command that opens it, as the menus run it.
    const open: Record<string, string> = {
      settings: "mcpp.openSettings",
      "graph:packages": "mcpp.project.showDependencyGraph",
      "graph:modules": "mcpp.project.showModuleDependencyGraph",
      library: "mcpp.library.openDetail",
    };
    const command = open[p.view];
    if (command) void commands.executeCommand(command, ...(p.args === undefined ? [] : [p.args]));
    else workbench.focus(p.view);
  });
}

start().catch((error) => {
  document.body.textContent = `mcpp: ${error instanceof Error ? error.message : String(error)}`;
  request("log", { text: `workbench: ${error instanceof Error ? error.stack ?? error.message : String(error)}\n` }).catch(() => undefined);
});
