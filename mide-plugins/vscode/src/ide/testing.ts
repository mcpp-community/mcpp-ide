/**
 * mcpp's tests in VS Code's Testing view (design 2026-10-30 part 2 §7). The tests of each project
 * come from `test/list` (mcpp test --list: no build), each a program located at its `main`; a
 * program of a test framework has its cases as children, located at their macros (`test/cases`,
 * read from the source), and after a build the program lists them (only where its source calls its
 * framework's macros, and only in a trusted workspace), which adds the cases macros generate.
 *
 * A run of whole programs is one test job, filtered to the program asked for; a run of cases is
 * `test/runCases`, whose program receives the framework's filter, and each `test/caseResult` marks
 * its case.
 */
import * as path from "node:path";
import * as vscode from "vscode";

import { t } from "../i18n/t";
import type { Jobs } from "./jobs";
import { baseCaseName, caseId, parseCaseId, parseTestId, testId } from "./mapping";
import type { IdeProjects } from "./projects";
import type { Session } from "./session";
import type { CaseResult, JobFinished, TestCases, TestResult } from "./protocol";

interface Program {
  root: string;
  member: string;
  test: string;
  framework: string | null;
}

export class Testing implements vscode.Disposable {
  private readonly controller = vscode.tests.createTestController("mcpp", "mcpp");
  private readonly disposables: vscode.Disposable[] = [this.controller];
  private readonly programs = new Map<string, Program>();

  constructor(private readonly session: Session, private readonly jobs: Jobs, private readonly projects: IdeProjects) {
    this.controller.resolveHandler = async (item) => { if (!item) await this.discover(); };
    this.controller.refreshHandler = async () => { await this.discover(true); };
    this.controller.createRunProfile(t("Run"), vscode.TestRunProfileKind.Run, (request, token) => this.run(request, token), true);
    this.disposables.push(
      session.on("manifest/changed", () => void this.discover()),
      session.on("result/changed", () => void this.discover()),
      session.onDidStart.event(() => void this.discover()),
      // A build ended: the programs can list the cases their macros generate.
      session.on("job/finished", (p: JobFinished) => {
        if (p.status === "ok" && (p.kind === "build" || p.kind === "test") && vscode.workspace.isTrusted) void this.discover(true);
      }),
      vscode.workspace.onDidSaveTextDocument((doc) => {
        if (doc.languageId === "cpp" && /[\\/]tests[\\/]/.test(doc.uri.fsPath)) void this.discover();
      }),
    );
  }

  /** The program a test's `main` is, by its project and name (the CodeLens runs it). */
  item(root: string, test: string): vscode.TestItem | undefined {
    const group = this.controller.items.get(root);
    let found: vscode.TestItem | undefined;
    group?.children.forEach((child) => {
      const id = parseTestId(child.id);
      if (id && id.test === test) found = child;
    });
    return found;
  }

  /** Runs one program or case, as the Testing view's ▶ does. */
  async runItem(item: vscode.TestItem): Promise<void> {
    const source = new vscode.CancellationTokenSource();
    try {
      await this.run(new vscode.TestRunRequest([item]), source.token);
    } finally {
      source.dispose();
    }
  }

  async discover(list = false): Promise<void> {
    for (const project of this.projects.all()) {
      let tests: { test: string; member: string; main: string }[] = [];
      try {
        tests = (await this.session.request<{ tests: { test: string; member: string; main: string }[] }>("test/list", { root: project.root })).tests;
      } catch {
        continue;
      }
      let group = this.controller.items.get(project.root);
      if (!group) {
        group = this.controller.createTestItem(project.root, project.name ?? path.basename(project.root), vscode.Uri.file(project.root));
        this.controller.items.add(group);
      }
      const items: vscode.TestItem[] = [];
      for (const test of tests) {
        const id = testId(project.root, test.member, test.test);
        const item = this.controller.createTestItem(id, test.member ? `${test.member} · ${test.test}` : test.test,
                                                    test.main ? vscode.Uri.file(test.main) : undefined);
        const program: Program = { root: project.root, member: test.member, test: test.test, framework: null };
        let cases: TestCases | undefined;
        try {
          cases = await this.session.request<TestCases>("test/cases", { root: project.root, test: test.test, list });
        } catch {
          cases = undefined;
        }
        // The program at its `main` (F11); a framework's program at its first case when it has none.
        const mainLine = cases?.main?.line ?? cases?.cases.find((c) => c.line !== undefined)?.line ?? 0;
        if (test.main) item.range = new vscode.Range(mainLine, 0, mainLine, 0);
        if (cases?.framework) {
          program.framework = cases.framework;
          item.description = cases.framework;
          item.children.replace(cases.cases.map((c) => {
            const child = this.controller.createTestItem(caseId(project.root, test.member, test.test, c.name), c.name,
                                                         test.main ? vscode.Uri.file(test.main) : undefined);
            if (test.main && c.line !== undefined) child.range = new vscode.Range(c.line, 0, c.line, 0);
            return child;
          }));
        }
        this.programs.set(id, program);
        items.push(item);
      }
      group.children.replace(items);
    }
  }

  private async run(request: vscode.TestRunRequest, token: vscode.CancellationToken): Promise<void> {
    const run = this.controller.createTestRun(request);
    // One job per program whose cases were asked for; one job per project for whole programs.
    const wholePrograms = new Map<string, vscode.TestItem[]>();
    const casesOf = new Map<string, vscode.TestItem[]>();
    const collect = (item: vscode.TestItem) => {
      if (request.exclude?.includes(item)) return;
      const c = parseCaseId(item.id);
      if (c) {
        const owner = testId(c.root, c.member, c.test);
        casesOf.set(owner, [...(casesOf.get(owner) ?? []), item]);
        return;
      }
      const id = parseTestId(item.id);
      if (!id) { item.children.forEach(collect); return; }
      wholePrograms.set(id.root, [...(wholePrograms.get(id.root) ?? []), item]);
    };
    if (request.include) request.include.forEach(collect);
    else this.controller.items.forEach(collect);

    for (const [owner, items] of casesOf) {
      if (token.isCancellationRequested) break;
      const program = this.programs.get(owner);
      if (!program?.framework) continue;
      items.forEach((i) => run.enqueued(i));
      // Every case of the program asked for: the filter is left out and the program runs them all.
      let all = 0;
      this.controller.items.get(program.root)?.children.get(owner)?.children.forEach(() => { all++; });
      const names = items.length === all ? [] : items.map((i) => parseCaseId(i.id)!.name);
      const byName = new Map(items.map((i) => [parseCaseId(i.id)!.name, i]));
      const seen = new Set<vscode.TestItem>();
      const handle = await this.jobs.start(
        { root: program.root, test: program.test, framework: program.framework, cases: names, color: true },
        {
          output: (text) => run.appendOutput(text.replace(/\r?\n/g, "\r\n")),
          caseResult: (r: CaseResult) => {
            const item = byName.get(r.case) ?? byName.get(baseCaseName(program.framework, r.case));
            if (!item) return;
            seen.add(item);
            if (r.status === "passed") run.passed(item, r.durationMs);
            else if (r.status === "skipped") run.skipped(item);
            else run.failed(item, new vscode.TestMessage(r.message || r.status), r.durationMs);
          },
        },
        "test/runCases",
      );
      const cancel = token.onCancellationRequested(() => void handle.cancel());
      const done = await handle.done;
      cancel.dispose();
      for (const i of items)
        if (!seen.has(i)) {
          if (done.status === "ok" || done.status === "failed") run.skipped(i);
          else run.errored(i, new vscode.TestMessage(t("The test program did not run: {0}", done.status)));
        }
    }

    for (const [root, items] of wholePrograms) {
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
