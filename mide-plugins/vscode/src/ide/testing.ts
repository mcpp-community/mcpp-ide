/**
 * mcpp's tests in VS Code's Testing view (design 2026-10-30 part 2 §7; design 2026-11-10). The tests of
 * each project come from `test/list` (mcpp test --list: no build), each a program located at its
 * `main`; a program of several files (`[test] programs = "directory"`) has its files as children,
 * each the part of the program a run gives a process. A program that implements the test protocol
 * has its cases as children of the file that declares them, as the program lists them (`test/cases`,
 * `mcpp test --cases`; only in a trusted workspace). A case carries the test tags `kind:<kind>` and its
 * own tags, so `@kind:bench` filters the view.
 *
 * A run of whole programs, or of one file's part, is one test job; a run of cases is `test/runCases`.
 * Results arrive per case as the program writes them when the project's mcpp states them
 * (`test/caseEvent`, test-case-stream), else with each part's record (`test/result` and its cases).
 * Each failed check is a message at its place: a diff when it states both sides, with its statement
 * and whether it ended the case; a case's own output is shown with it; after a run each case's line
 * ends with its verdict, its duration, the checks that failed and its metrics against their bounds.
 *
 * NOTHING HERE KNOWS A TEST FRAMEWORK (design 2026-11-10 supplement, T14). Cases are what a program
 * that implements the test protocol lists (mcpp test spec B3) and are run by identifier (P4); a program
 * of no protocol is run whole. Tests run beside each other: each is a job of its own.
 */
import * as path from "node:path";
import * as vscode from "vscode";

import { t } from "../i18n/t";
import type { Jobs } from "./jobs";
import { caseId, caseSummary, caseTags, fileId, parseCaseId, parseFileId, parseTestId, testId } from "./mapping";
import type { IdeProjects } from "./projects";
import type { Session } from "./session";
import type { CaseEvent, CaseFailure, CaseOutcome, CaseResult, FailureLocation, JobFinished, TestCases, TestProgram, TestResult } from "./protocol";

/** Text for the test terminal, whose lines end in `\r\n`. */
function terminal(text: string): string {
  return text.replace(/\r?\n/g, "\r\n");
}

/**
 * Where a failure is shown (D16): the place its output names, in the program's source when the
 * name is relative to the project; else the item's own line (a test's `main`, a case's macro).
 */
function failureAt(root: string, location: FailureLocation | null | undefined, item: vscode.TestItem): vscode.Location | undefined {
  if (location) {
    const file = path.isAbsolute(location.path) ? location.path : path.join(root, location.path);
    return new vscode.Location(vscode.Uri.file(file), new vscode.Position(location.line, 0));
  }
  return item.uri && item.range ? new vscode.Location(item.uri, item.range) : undefined;
}

/** The output a result carries, said to its item, with a note when the record cut it short. */
function attribute(run: vscode.TestRun, item: vscode.TestItem, output: string | undefined, truncated: boolean | undefined,
                   at: vscode.Location | undefined): void {
  if (!output) return;
  run.appendOutput(terminal(output.endsWith("\n") ? output : `${output}\n`), at, item);
  if (truncated) run.appendOutput(terminal(`${t("(the output was longer; its head and tail are shown)")}\n`), at, item);
}

/** One failed check, as a person reads it: what did not hold, the statement, and whether the case went on. */
function checkText(f: CaseFailure): string {
  const lines = [f.message];
  if (f.source && f.source !== f.message) lines.push(f.source);
  if (f.fatal) lines.push(t("fatal: the rest of the case did not run"));
  return lines.join("\n");
}

/**
 * A failed case's messages (mcpp test spec O3): one per check that did not hold, at its place, a diff
 * when it states both sides (VS Code compares them); without a failure, the case's reason.
 */
function failureMessages(root: string, o: CaseOutcome, item: vscode.TestItem): vscode.TestMessage[] {
  if (o.unexpectedPass) {
    const m = new vscode.TestMessage(t("The case passed, though it is expected to fail."));
    m.location = failureAt(root, o.location, item);
    return [m];
  }
  const failures = o.failures ?? [];
  if (failures.length === 0) {
    const m = new vscode.TestMessage(o.message || o.reason || o.status);
    m.location = failureAt(root, o.location, item);
    return [m];
  }
  return failures.map((f) => {
    const m = f.expected !== undefined && f.actual !== undefined
      ? vscode.TestMessage.diff(checkText(f), f.expected, f.actual)
      : new vscode.TestMessage(checkText(f));
    m.location = failureAt(root, { path: f.path, line: f.line }, item);
    return m;
  });
}

interface Program {
  root: string;
  member: string;
  test: string;
  /** The program implements the test protocol: its cases are listed and run by identifier. */
  protocol: boolean;
  /** The framework's name the project states, shown beside the program. */
  framework: string | null;
  /** The cases by name, and the files' items by part. */
  cases: Map<string, vscode.TestItem>;
  parts: Map<string, vscode.TestItem>;
}

/** After a run, each case's line ends with what the run said of it. */
class CaseDecorations implements vscode.Disposable {
  private readonly type = vscode.window.createTextEditorDecorationType({
    after: { color: new vscode.ThemeColor("descriptionForeground"), margin: "0 0 0 2em" },
  });
  private readonly byFile = new Map<string, Map<number, string>>();
  private readonly disposables: vscode.Disposable[] = [
    this.type,
    vscode.window.onDidChangeVisibleTextEditors(() => this.apply()),
    vscode.workspace.onDidChangeTextDocument((e) => { if (this.byFile.delete(e.document.uri.fsPath)) this.apply(); }),
  ];

  set(item: vscode.TestItem, text: string): void {
    if (!item.uri || !item.range) return;
    const lines = this.byFile.get(item.uri.fsPath) ?? new Map<number, string>();
    lines.set(item.range.start.line, text);
    this.byFile.set(item.uri.fsPath, lines);
    this.apply();
  }

  clear(files: Iterable<string>): void {
    for (const f of files) this.byFile.delete(f);
    this.apply();
  }

  private apply(): void {
    for (const editor of vscode.window.visibleTextEditors) {
      const lines = this.byFile.get(editor.document.uri.fsPath);
      editor.setDecorations(this.type, [...(lines ?? [])].map(([line, text]) => ({
        range: new vscode.Range(line, Number.MAX_SAFE_INTEGER, line, Number.MAX_SAFE_INTEGER),
        renderOptions: { after: { contentText: text } },
      })));
    }
  }

  dispose(): void {
    this.disposables.forEach((d) => d.dispose());
  }
}

export class Testing implements vscode.Disposable {
  private readonly controller = vscode.tests.createTestController("mcpp", "mcpp");
  private readonly decorations = new CaseDecorations();
  private readonly disposables: vscode.Disposable[] = [this.controller, this.decorations];
  private readonly programs = new Map<string, Program>();
  /** A discovery the events asked for, run once they settle: a save, a build's end and a changed result arrive together. */
  private pending: { timer: ReturnType<typeof setTimeout>; list: boolean } | undefined;

  constructor(private readonly session: Session, private readonly jobs: Jobs, private readonly projects: IdeProjects) {
    this.controller.resolveHandler = async (item) => { if (!item) await this.discover(vscode.workspace.isTrusted); };
    this.controller.refreshHandler = async () => { await this.discover(true); };
    this.controller.createRunProfile(t("Run"), vscode.TestRunProfileKind.Run, (request, token) => this.run(request, token), true);
    this.disposables.push(
      session.on("manifest/changed", () => this.discoverSoon()),
      session.on("result/changed", () => this.discoverSoon()),
      session.onDidStart.event(() => this.discoverSoon(vscode.workspace.isTrusted)),
      // A build ended: the programs list their cases (mcpp test spec B3).
      session.on("job/finished", (p: JobFinished) => {
        if (p.status === "ok" && (p.kind === "build" || p.kind === "test") && vscode.workspace.isTrusted) this.discoverSoon(true);
      }),
      vscode.workspace.onDidSaveTextDocument((doc) => {
        if (doc.languageId === "cpp" && /[\\/]tests[\\/]/.test(doc.uri.fsPath)) this.discoverSoon();
      }),
      { dispose: () => { if (this.pending) clearTimeout(this.pending.timer); } },
    );
  }

  /** A discovery after the events of a moment settle (400 ms), listing when any of them asked for it. */
  private discoverSoon(list = false): void {
    const wanted = list || (this.pending?.list ?? false);
    if (this.pending) clearTimeout(this.pending.timer);
    this.pending = {
      list: wanted,
      timer: setTimeout(() => {
        this.pending = undefined;
        void this.discover(wanted);
      }, 400),
    };
  }

  /** The program a test's `main` is, or the item of one file's part of it (the CodeLens and the Run menu run it). */
  item(root: string, test: string, part?: string): vscode.TestItem | undefined {
    const program = this.programs.get(this.findProgramId(root, test) ?? "");
    if (part && program) return program.parts.get(part);
    return this.controller.items.get(root)?.children.get(this.findProgramId(root, test) ?? "");
  }

  private findProgramId(root: string, test: string): string | undefined {
    for (const [id, p] of this.programs) if (p.root === root && p.test === test) return id;
    return undefined;
  }

  /** Runs one program, file or case, as the Testing view's ▶ does. */
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
      let tests: TestProgram[] = [];
      try {
        tests = (await this.session.request<{ tests: TestProgram[] }>("test/list", { root: project.root })).tests;
      } catch {
        continue;
      }
      let group = this.controller.items.get(project.root);
      if (!group) {
        group = this.controller.createTestItem(project.root, project.name ?? path.basename(project.root), vscode.Uri.file(project.root));
        this.controller.items.add(group);
      }
      const items: vscode.TestItem[] = [];
      for (const test of tests) items.push(await this.programItem(project.root, test, list));
      group.children.replace(items);
    }
  }

  private async programItem(root: string, test: TestProgram, list: boolean): Promise<vscode.TestItem> {
    const id = testId(root, test.member, test.test);
    const home = test.main || test.files?.[0] || "";
    const item = this.controller.createTestItem(id, test.member ? `${test.member} · ${test.test}` : test.test, home ? vscode.Uri.file(home) : undefined);
    const program: Program = { root, member: test.member, test: test.test, protocol: false, framework: null, cases: new Map(), parts: new Map() };
    let cases: TestCases | undefined;
    try {
      cases = await this.session.request<TestCases>("test/cases", { root, test: test.test, list });
    } catch {
      cases = undefined;
    }
    // The program at its `main` (F11); a program of cases at its first case when it has none.
    const mainLine = cases?.main?.line ?? cases?.cases.find((c) => c.line !== undefined)?.line ?? 0;
    if (home) item.range = new vscode.Range(mainLine, 0, mainLine, 0);
    if (cases?.protocol) {
      program.protocol = true;
      program.framework = cases.framework;
      if (cases.framework) item.description = cases.framework;
    }
    const caseItem = (c: TestCases["cases"][number]): vscode.TestItem => {
      const file = c.file || home;
      const child = this.controller.createTestItem(caseId(root, test.member, test.test, c.name), c.name, file ? vscode.Uri.file(file) : undefined);
      if (file && c.line !== undefined) child.range = new vscode.Range(c.line, 0, c.line, 0);
      child.tags = caseTags(c.kind, c.tags).map((tag) => new vscode.TestTag(tag));
      if (c.kind && c.kind !== "unit") child.description = c.kind;
      program.cases.set(c.name, child);
      return child;
    };
    const parts = test.layout === "directory" ? test.parts ?? [] : [];
    if (parts.length > 0) {
      // A program of several files: each file is the part a run gives a process, its cases beneath it.
      item.children.replace(parts.map((part) => {
        const child = this.controller.createTestItem(fileId(root, test.member, test.test, part.name), path.basename(part.file), vscode.Uri.file(part.file));
        const own = (cases?.cases ?? []).filter((c) => c.file === part.file);
        child.range = new vscode.Range(own.find((c) => c.line !== undefined)?.line ?? 0, 0, own.find((c) => c.line !== undefined)?.line ?? 0, 0);
        child.children.replace(program.protocol ? own.map(caseItem) : []);
        program.parts.set(part.name, child);
        return child;
      }));
    } else if (program.protocol) {
      item.children.replace((cases?.cases ?? []).map(caseItem));
    }
    this.programs.set(id, program);
    return item;
  }

  /** A case's outcome, marked on its item; its line then ends with what the run said. */
  private markCase(run: vscode.TestRun, program: Program, o: CaseOutcome, seen: Set<vscode.TestItem>, output?: string, truncated?: boolean): void {
    const item = program.cases.get(o.case);
    if (!item) return;
    seen.add(item);
    const at = o.status === "failed" ? failureAt(program.root, o.location, item) : undefined;
    // Every case's own output reaches its item, passed or not (D16): what it wrote (O8), else what the record carries.
    attribute(run, item, o.output || output, truncated, at);
    const ms = o.durationMs;
    if (o.status === "passed") run.passed(item, ms);
    else if (o.status === "skipped") {
      if (o.reason) run.appendOutput(terminal(`${o.reason}\n`), undefined, item);
      run.skipped(item);
    } else if (o.status === "errored") run.errored(item, failureMessages(program.root, o, item), ms);
    else run.failed(item, failureMessages(program.root, o, item), ms);
    this.decorations.set(item, caseSummary(o));
  }

  private async run(request: vscode.TestRunRequest, token: vscode.CancellationToken): Promise<void> {
    const run = this.controller.createTestRun(request);
    // One job per program whose cases were asked for; one job per project for whole programs and parts.
    const whole = new Map<string, { item: vscode.TestItem; filter: string }[]>();
    const casesOf = new Map<string, vscode.TestItem[]>();
    const collect = (item: vscode.TestItem) => {
      if (request.exclude?.includes(item)) return;
      const c = parseCaseId(item.id);
      if (c) {
        const owner = testId(c.root, c.member, c.test);
        casesOf.set(owner, [...(casesOf.get(owner) ?? []), item]);
        return;
      }
      const f = parseFileId(item.id);
      if (f) {
        whole.set(f.root, [...(whole.get(f.root) ?? []), { item, filter: f.part }]);
        return;
      }
      const id = parseTestId(item.id);
      if (!id) { item.children.forEach(collect); return; }
      whole.set(id.root, [...(whole.get(id.root) ?? []), { item, filter: id.test }]);
    };
    if (request.include) request.include.forEach(collect);
    else this.controller.items.forEach(collect);

    for (const [owner, items] of casesOf) {
      if (token.isCancellationRequested) break;
      await this.runCases(run, token, owner, items);
    }
    for (const [root, targets] of whole) {
      if (token.isCancellationRequested) break;
      await this.runWhole(run, token, root, targets, request.include !== undefined);
    }
    run.end();
  }

  private async runCases(run: vscode.TestRun, token: vscode.CancellationToken, owner: string, items: vscode.TestItem[]): Promise<void> {
    const program = this.programs.get(owner);
    if (!program?.protocol) return;
    items.forEach((i) => run.enqueued(i));
    // Every case of the program asked for: the filter is left out and the program runs them all.
    const names = items.length === program.cases.size ? [] : items.map((i) => parseCaseId(i.id)!.name);
    const seen = new Set<vscode.TestItem>();
    const handle = await this.jobs.start(
      { root: program.root, test: program.test, cases: names, color: true },
      {
        output: (text) => run.appendOutput(terminal(text)),
        caseEvent: (e: CaseEvent) => this.onCaseEvent(run, program, e, seen),
        caseResult: (r: CaseResult) => {
          const item = program.cases.get(r.case);
          if (item && seen.has(item)) return;
          this.markCase(run, program, r, seen, r.output, r.outputTruncated);
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

  /** A case begins (VS Code shows it running) or ends, as the program writes it (test-case-stream). */
  private onCaseEvent(run: vscode.TestRun, program: Program, e: CaseEvent, seen: Set<vscode.TestItem>): void {
    if (e.event === "begin") {
      const item = program.cases.get(e.case);
      if (item) run.started(item);
    } else if (e.status) {
      this.markCase(run, program, e as CaseOutcome, seen);
    }
  }

  private async runWhole(run: vscode.TestRun, token: vscode.CancellationToken, root: string,
                         targets: { item: vscode.TestItem; filter: string }[], included: boolean): Promise<void> {
    targets.forEach((target) => run.enqueued(target.item));
    const filter = targets.length === 1 && included ? targets[0].filter : undefined;
    const programs = [...this.programs.values()].filter((p) => p.root === root);
    const programOf = (name: string) => programs.find((p) => p.test === name);
    this.decorations.clear(programs.flatMap((p) => [...p.cases.values()].map((c) => c.uri?.fsPath ?? "")));
    const seen = new Set<vscode.TestItem>();
    const handle = await this.jobs.start({ kind: "test", root, filter, color: true }, {
      output: (text) => run.appendOutput(terminal(text)),
      caseEvent: (e: CaseEvent) => {
        const program = programOf(e.program);
        if (program) this.onCaseEvent(run, program, e, seen);
      },
      testResult: (r: TestResult) => {
        const program = programOf(r.program ?? r.test);
        const item = (r.part ? program?.parts.get(r.part) : undefined) ?? this.item(root, r.program ?? r.test);
        for (const o of r.cases ?? []) {
          const caseItem = program?.cases.get(o.case);
          if (program && !(caseItem && seen.has(caseItem))) this.markCase(run, program, o, seen, o.status === "failed" ? o.message : undefined);
        }
        if (!item) return;
        const ms = r.durationMs;
        const failing = r.status === "failed" || r.status === "timedOut" || r.status === "errored";
        const at = failing ? failureAt(root, r.location, item) : undefined;
        // The program's output reaches its item whatever the result (D16).
        attribute(run, item, r.output, r.outputTruncated, at);
        if (r.status === "passed") run.passed(item, ms);
        else if (r.status === "skipped") {
          if (r.reason) run.appendOutput(terminal(`${r.reason}\n`), undefined, item);
          run.skipped(item);
        } else {
          const headline = r.status === "timedOut" ? t("The test timed out.")
            : r.compileFailed ? t("The test did not compile.") : undefined;
          const message = new vscode.TestMessage([headline, r.output || r.reason || r.status].filter(Boolean).join("\n"));
          message.location = at;
          if (r.status === "errored") run.errored(item, message, ms); else run.failed(item, message, ms);
        }
      },
    });
    const cancel = token.onCancellationRequested(() => void handle.cancel());
    const done = await handle.done;
    cancel.dispose();
    if (done.status !== "ok" && done.status !== "failed")
      for (const target of targets) run.skipped(target.item);
  }

  dispose(): void {
    this.disposables.forEach((d) => d.dispose());
  }
}
