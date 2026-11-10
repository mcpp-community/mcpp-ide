/**
 * Test Insights (design 2026-11-10): what the last runs of a project's tests say -- the run, its
 * failures and since when, each metric against its bound, the slowest and the flaky cases, the
 * schedule with its critical path -- read by the backend from the store mcpp keeps (`test/insights`)
 * and drawn by the page mcpp-clion shows in JCEF (webui/src/tests.ts), here in a webview. It refreshes
 * when a test job ends; a click on a failure opens its place.
 */
import * as path from "node:path";
import * as vscode from "vscode";

import { TOOL_COMMANDS } from "../commands/ids";
import { t } from "../i18n/t";
import type { IdeProjects } from "./projects";
import type { JobFinished } from "./protocol";
import type { Session } from "./session";
import { WebviewDocument } from "../webview/document";

/** The page's strings, in the session's language. */
export function insightsLabels(): Record<string, string> {
  return {
    title: t("Test Insights"),
    noRun: t("No run is kept yet: mcpp test keeps each run in {0} (mcpp 2026.11.9.1 and later)."),
    window: t("{0} runs kept · the last began {1}"),
    failures: t("Failures"),
    fresh: t("new"),
    since: t("since {0}"),
    expected: t("expected"),
    actual: t("actual"),
    metrics: t("Metrics"),
    slowest: t("Slowest"),
    median: t("median {0}"),
    flaky: t("Flaky"),
    flakyCounts: t("{0} passed · {1} failed · {2} changes"),
    schedule: t("Schedule"),
    worker: t("worker {0}"),
    criticalPath: t("Critical path"),
    noMetrics: t("Metrics appear when the project's mcpp states them (test-metrics, mcpp#917)."),
  };
}

export function registerTestInsights(context: vscode.ExtensionContext, session: Session, projects: IdeProjects): vscode.Disposable {
  const scripts = vscode.Uri.joinPath(context.extensionUri, "dist", "webui");
  let panel: vscode.WebviewPanel | undefined;
  let root: string | undefined;
  // The page brings its own style (webui/src/tests.ts); the document holds the view's nonce.
  const document = new WebviewDocument("tests.css");

  const render = async (): Promise<void> => {
    if (!panel || !root) return;
    let insights: unknown;
    try {
      insights = await session.request("test/insights", { root });
    } catch (error) {
      void vscode.window.showErrorMessage(t("mcpp: {0}", String(error instanceof Error ? error.message : error)));
      return;
    }
    await panel.webview.postMessage({ insights, labels: insightsLabels() });
  };

  const open = async (argument?: { root?: string }): Promise<void> => {
    root = argument?.root ?? projects.current()?.root;
    if (!root) {
      void vscode.window.showInformationMessage(t("Open an mcpp project first."));
      return;
    }
    if (!panel) {
      panel = vscode.window.createWebviewPanel("mcpp.testInsights", t("Test Insights"), vscode.ViewColumn.Active,
                                               { enableScripts: true, retainContextWhenHidden: true, localResourceRoots: [scripts] });
      const script = panel.webview.asWebviewUri(vscode.Uri.joinPath(scripts, "tests.js")).toString();
      const { nonce } = document.assets(scripts, panel.webview);
      document.paint(panel.webview, `<!doctype html><html><head><meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'nonce-${nonce}'; style-src 'unsafe-inline';">
<script nonce="${nonce}" src="${script}"></script></head><body class="vscode"><div id="tests"></div>
<script nonce="${nonce}">const host = acquireVsCodeApi(); window.mcppHostSend = (text) => host.postMessage(text);
window.addEventListener("message", (e) => window.mcppRender(e.data)); host.postMessage("ready");</script></body></html>`);
      panel.webview.onDidReceiveMessage(async (message: unknown) => {
        if (message === "ready") return render();
        const at = typeof message === "string" ? (JSON.parse(message) as { open?: { path: string; line: number } }).open : undefined;
        if (!at || !root) return;
        const file = path.isAbsolute(at.path) ? at.path : path.join(root, at.path);
        const editor = await vscode.window.showTextDocument(vscode.Uri.file(file), { viewColumn: vscode.ViewColumn.One });
        const position = new vscode.Position(at.line, 0);
        editor.selection = new vscode.Selection(position, position);
        editor.revealRange(new vscode.Range(position, position), vscode.TextEditorRevealType.InCenter);
      });
      panel.onDidDispose(() => { panel = undefined; document.invalidate(); });
    } else {
      panel.reveal();
      await render();
    }
  };

  return vscode.Disposable.from(
    vscode.commands.registerCommand(TOOL_COMMANDS.testInsights, open),
    // A run of the tests ended: the store holds another run.
    session.on("job/finished", (p: JobFinished) => { if (p.kind === "test" && p.root === root) void render(); }),
    { dispose: () => panel?.dispose() },
  );
}
