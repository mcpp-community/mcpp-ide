// mcpp's tests in VS Code's Testing view. The tests of each project come from `test/list` (mcpp
// test --list: no build), each item located at the file that holds its `main`; a run is one test
// job, filtered to the test asked for, and each `test/result` marks its item.
import * as vscode from "vscode";

import type { Jobs } from "./jobs";
import { parseTestId, testId } from "./mapping";
import type { Projects } from "./projects";
import type { Session } from "./session";
import type { TestResult } from "./protocol";

export class Testing implements vscode.Disposable {
  private readonly controller = vscode.tests.createTestController("mcpp", "mcpp");
  private readonly disposables: vscode.Disposable[] = [this.controller];

  constructor(private readonly session: Session, private readonly jobs: Jobs, private readonly projects: Projects) {
    this.controller.resolveHandler = async (item) => { if (!item) await this.discover(); };
    this.controller.refreshHandler = async () => { await this.discover(); };
    this.controller.createRunProfile(session.t("Run"), vscode.TestRunProfileKind.Run, (request, token) => this.run(request, token), true);
    this.disposables.push(
      session.on("manifest/changed", () => void this.discover()),
      session.on("result/changed", () => void this.discover()),
      session.onDidStart.event(() => void this.discover()),
    );
  }

  async discover(): Promise<void> {
    for (const project of this.projects.all) {
      let tests: { test: string; member: string; main: string }[] = [];
      try {
        tests = (await this.session.request<{ tests: { test: string; member: string; main: string }[] }>("test/list", { root: project.root })).tests;
      } catch {
        continue;
      }
      let group = this.controller.items.get(project.root);
      if (!group) {
        group = this.controller.createTestItem(project.root, project.name, vscode.Uri.file(project.root));
        this.controller.items.add(group);
      }
      const items = tests.map((t) => {
        const item = this.controller.createTestItem(testId(project.root, t.member, t.test), t.member ? `${t.member} · ${t.test}` : t.test,
                                                    t.main ? vscode.Uri.file(t.main) : undefined);
        if (t.main) item.range = new vscode.Range(0, 0, 0, 0);
        return item;
      });
      group.children.replace(items);
    }
  }

  private async run(request: vscode.TestRunRequest, token: vscode.CancellationToken): Promise<void> {
    const run = this.controller.createTestRun(request);
    // One job per project; a single test asked for becomes the job's filter.
    const byRoot = new Map<string, vscode.TestItem[]>();
    const collect = (item: vscode.TestItem) => {
      const id = parseTestId(item.id);
      if (!id) { item.children.forEach(collect); return; }
      if (request.exclude?.includes(item)) return;
      if (!byRoot.has(id.root)) byRoot.set(id.root, []);
      byRoot.get(id.root)!.push(item);
    };
    if (request.include) request.include.forEach(collect);
    else this.controller.items.forEach(collect);
    for (const [root, items] of byRoot) {
      if (token.isCancellationRequested) break;
      items.forEach((i) => run.enqueued(i));
      const index = new Map(items.map((i) => [parseTestId(i.id)!.test, i]));
      const filter = items.length === 1 && request.include ? parseTestId(items[0].id)!.test : undefined;
      const handle = await this.jobs.start({ kind: "test", root, filter, color: true }, {
        output: (text) => run.appendOutput(text.replace(/\r?\n/g, "\r\n")),
        testResult: (r: TestResult) => {
          const item = index.get(r.test);
          if (!item) return;
          const ms = r.durationMs;
          if (r.status === "passed") run.passed(item, ms);
          else if (r.status === "skipped") run.skipped(item);
          else {
            const message = new vscode.TestMessage(r.output || r.reason || r.status);
            if (r.status === "errored") run.errored(item, message, ms); else run.failed(item, message, ms);
          }
        },
      });
      const cancel = token.onCancellationRequested(() => void handle.cancel());
      const done = await handle.done;
      cancel.dispose();
      if (done.status !== "ok" && done.status !== "failed")
        for (const i of items) run.skipped(i);
    }
    run.end();
  }

  dispose(): void {
    this.disposables.forEach((d) => d.dispose());
  }
}
