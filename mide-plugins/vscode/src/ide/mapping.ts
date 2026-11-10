// The protocol's values in VS Code's terms, as pure functions (the unit tests run them without an
// editor): positions, severities, test identities, the debuggers present, the "when" of a command.
import type { CaseOutcome, Diagnostic, MetricBound, Position } from "./protocol";

/** mcpp's positions are 1-based lines and columns; VS Code's are 0-based. */
export function zeroBased(p: Position | undefined): { line: number; character: number } {
  if (!p) return { line: 0, character: 0 };
  return { line: Math.max(0, p.line - 1), character: Math.max(0, p.column - 1) };
}

export type Severity = "error" | "warning" | "information" | "hint";

export function severityOf(d: Pick<Diagnostic, "severity">): Severity {
  switch (d.severity) {
    case "error": return "error";
    case "warning": return "warning";
    case "note": case "help": return "hint";
    default: return "information";
  }
}

/** One identity per test: its project, its workspace member and its name. */
export function testId(root: string, member: string, test: string): string {
  return `${root}::${member}::${test}`;
}

/** What separates a case's name from its program's id: a name is the framework's, and may hold "::". */
const CASE = "\u001f";

/** A case of a test program (design 2026-10-30 part 2 §7): its program's id, then its full name. */
export function caseId(root: string, member: string, test: string, name: string): string {
  return `${testId(root, member, test)}${CASE}${name}`;
}

export function parseCaseId(id: string): { root: string; member: string; test: string; name: string } | undefined {
  const at = id.indexOf(CASE);
  if (at < 0) return undefined;
  const program = parseTestId(id.slice(0, at));
  return program === undefined ? undefined : { ...program, name: id.slice(at + 1) };
}

/** A case result's name as its source names it: GoogleTest's instance prefix and parameter dropped. */
export function baseCaseName(framework: string | null, name: string): string {
  if (framework !== "gtest") return name;
  const dot = name.indexOf(".");
  if (dot < 0) return name;
  const suiteParts = name.slice(0, dot).split("/").filter((part) => part !== "" && !/^\d+$/.test(part));
  const test = name.slice(dot + 1).split("/")[0];
  return `${suiteParts[suiteParts.length - 1] ?? ""}.${test}`;
}

/** VS Code's SymbolKind.Function (LSP numbers it 12). */
const FUNCTION_SYMBOL = 11;

/** The line of `main` among a document's symbols: a function named main at the top level. */
export function mainLineOf(symbols: readonly { name: string; kind: number; range: { start: { line: number } } }[] | undefined): number | undefined {
  return symbols?.find((s) => s.kind === FUNCTION_SYMBOL && (s.name === "main" || s.name.startsWith("main(")))?.range.start.line;
}

/** What separates a file of a program of several files from its program's id. */
const FILE = "\u001e";

/** A file of a program of several files (design 2026-11-10): its program's id, then the part it is. */
export function fileId(root: string, member: string, test: string, part: string): string {
  return `${testId(root, member, test)}${FILE}${part}`;
}

export function parseFileId(id: string): { root: string; member: string; test: string; part: string } | undefined {
  const at = id.indexOf(FILE);
  if (at < 0 || id.includes(CASE)) return undefined;
  const program = parseTestId(id.slice(0, at));
  return program === undefined ? undefined : { ...program, part: id.slice(at + 1) };
}

export function parseTestId(id: string): { root: string; member: string; test: string } | undefined {
  if (id.includes(CASE) || id.includes(FILE)) return undefined;
  const parts = id.split("::");
  if (parts.length < 3) return undefined;
  const test = parts.pop()!;
  const member = parts.pop()!;
  return { root: parts.join("::"), member, test };
}

/** The debuggers the backend may configure, from the extensions present. */
export function availableDebuggers(installed: (id: string) => boolean): string[] {
  const out: string[] = [];
  if (installed("vadimcn.vscode-lldb")) out.push("lldb");
  if (installed("ms-vscode.cpptools")) out.push("cppdbg");
  if (installed("llvm-vs-code-extensions.lldb-dap")) out.push("lldb-dap");
  if (installed("webfreak.debug")) out.push("gdb");
  return out;
}

/** The mcppls extension's id on the Marketplace. */
export const MCPPLS_EXTENSION = "sunrisepeak.mcpp-language-server";

/** Substitutes `{0}`, `{1}` in a runtime string. */
export function format(template: string, args: unknown[]): string {
  return args.reduce<string>((text, a, i) => text.split(`{${i}}`).join(String(a)), template);
}

/** The panel title "mcpp: <title>", the way every panel of this extension is named. */
export function panelTitle(title: string): string {
  return `mcpp: ${title}`;
}

/** A metric's value with its unit, three significant digits: `4.2 s`, `0.913`, `1250 ms`. */
export function measured(value: number, unit?: string): string {
  const text = Math.abs(value) >= 100 ? value.toFixed(0) : Number(value.toPrecision(3)).toString();
  return unit && unit !== "1" ? `${text} ${unit}` : text;
}

/** A metric against its bound: `ready 4.2 s ≤ 15 s`, `recall 0.93 ≥ 0.9`; without a bound, its value. */
export function metricText(name: string, value: number, unit: string | undefined, bound: MetricBound | undefined): string {
  if (bound?.max !== undefined) return `${name} ${measured(value, unit)} ≤ ${measured(bound.max, unit)}`;
  if (bound?.min !== undefined) return `${name} ${measured(value, unit)} ≥ ${measured(bound.min, unit)}`;
  return `${name} ${measured(value, unit)}`;
}

/** What follows a case's line after a run: its verdict, its duration, its metrics against their bounds. */
export function caseSummary(o: Pick<CaseOutcome, "status" | "durationMs" | "metrics" | "bounds" | "expectedFailure">): string {
  const mark = o.status === "passed" ? "✓" : o.status === "skipped" ? "○" : "✗";
  const parts = [`${mark}${o.expectedFailure ? " xfail" : ""}`];
  if (o.durationMs !== undefined && o.status !== "skipped") parts.push(`${o.durationMs} ms`);
  for (const m of o.metrics ?? [])
    parts.push(metricText(m.name, m.value, m.unit, o.bounds?.find((b) => b.metric === m.name)));
  return parts.join(" · ");
}

/** The test tags of a case: `kind:<kind>` and each of its own (VS Code's `@tag` filter selects by them). */
export function caseTags(kind: string | undefined, tags: readonly string[] | undefined): string[] {
  return [...(kind ? [`kind:${kind}`] : []), ...(tags ?? [])];
}
