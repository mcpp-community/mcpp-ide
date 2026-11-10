// The shapes of protocol/protocol.schema.json this adapter reads. The schema is the authority; these
// declarations only name what the code below touches.

export interface Project {
  root: string;
  workspaceRoot: string;
  name: string;
  isWorkspaceRoot: boolean;
}

export interface Check {
  id: string;
  status: "ok" | "info" | "warning" | "error";
  message: string;
}

/** One of mcppls's actions under each editor's own id (mcppls S3 section 8). */
export interface ServerAction {
  id: string;
  title: string;
  vscode?: string;
  vscodeArguments?: unknown[];
  jetbrains?: string;
  nvim?: string;
  command?: string;
}

export interface LanguageServiceStatus {
  state: "ok" | "outdated" | "missing" | "unreadable";
  program: string;
  source: string;
  version: string;
  required: string;
  message: string;
  actions: { id: "install" | "update"; title: string }[];
  /** What mcppls says about the workspace: `mcppls status` (0.0.20 and later) or the frozen table. */
  root?: string;
  statusSource?: "status" | "frozen" | "none";
  statusError?: string;
  capabilities?: string[];
  serverActions?: ServerAction[];
  workspace?: Record<string, unknown> | null;
  instances?: unknown[];
}

/** `project/mcpp` (design 2026-10-30 part 2 §4.4): the project's mcpp, asked in the project. */
export interface ProjectMcpp {
  root: string;
  state: "ok" | "pinned-missing" | "failed" | "not-probed";
  version: string;
  program: string;
  features: string[];
  featuresFrozen: boolean;
  fromSetting: boolean;
  message?: string;
  install?: string;
}

export interface InitializeResult {
  server: { name: string; version: string };
  protocol: number;
  mcpp: { path: string; version: string; contract: boolean } | null;
  languageService: LanguageServiceStatus | null;
  checks: Check[];
  projects: Project[];
  capabilities: string[];
}

export interface CommandAction {
  kind: string;
  job?: string;
  plan?: string;
  panel?: string;
  graph?: string;
  method?: string;
  commands?: string[];
  vscode?: string;
  args?: unknown[];
  then?: string;
  refresh?: boolean;
}

export interface CommandEntry {
  id: string;
  title: string;
  group: string;
  groupTitle: string;
  when?: string;
  quickMenu?: boolean;
  deprecated?: boolean;
  action: CommandAction;
}

export interface Position {
  line: number;
  column: number;
}

export interface Diagnostic {
  severity: string;
  message: string;
  code?: string;
  path?: string;
  range?: { start: Position; end: Position };
  source?: string;
}

export interface Platform {
  name?: string;
  version?: string | null;
  source?: string;
  fields?: Record<string, { value?: unknown; source?: string }>;
}

export interface ProjectSummary {
  root: string;
  targets: { name: string; kind: string; configuration: string }[];
  tests: { name: string; kind: string; configuration: string }[];
  libraries: { name: string; kind: string; configuration: string }[];
  toolchain: string;
  target?: string | null;
  lastBuild?: { status: string; finishedAt?: number | null; client?: string; command?: string };
  artifacts?: { kind: string; name: string; path: string }[];
  platform?: Platform | null;
  platformText?: string;
  hostPlatformText?: string;
  error?: string;
}

export interface Launch {
  argv: string[];
  cwd: string;
  env: Record<string, string>;
  program?: string;
  via?: string | null;
}

export interface TargetRow {
  target: string;
  note: string;
  pin: string;
  host: boolean;
  toolchains: { spec: string; status: string; default: boolean }[];
  platform: Platform | null;
  platformText: string;
  selected: boolean;
  installed: boolean;
}

export interface CleanPlan {
  action: string;
  argv: string[];
  level: number;
  preview: boolean | { source: string; text?: string; bytesText?: string; count?: number };
  acknowledge: boolean;
  title: string;
  detail: string;
  refusedUntrusted: boolean;
}

export interface JobFinished {
  jobId: string;
  status: "ok" | "failed" | "cancelled" | "denied" | "superseded";
  exitCode: number | null;
  durationMs?: number;
  root?: string;
  kind?: string;
  envelope?: { kind?: string; data?: unknown };
  diagnostics?: Diagnostic[];
  artifacts?: { kind: string; name: string; path: string }[];
  /** A launch job's program, described (mcpp's `mcpp.launch` data). */
  launch?: { argv: string[]; cwd: string; env?: Record<string, string>; program?: string };
}

/** `source/entries` (design 2026-10-30 part 2 §6.3): what a C++ file is to its project. */
export interface SourceEntries {
  file: string;
  root?: string;
  kind: "program" | "test" | "none";
  test?: string;
  /** A file of a program of several files: the part of the program it is (`unit/a`); `test` is the program. */
  part?: string;
  main?: { line: number; approximate?: boolean };
}

/** The bounds a case states on its metrics (mtest protocol P6.5). */
export interface MetricBound {
  metric: string;
  max?: number;
  min?: number;
}

/** What a case measured (mtest protocol P6), when mcpp states it (test-metrics, mcpp#917). */
export interface Metric {
  name: string;
  value: number;
  unit?: string;
  better?: "lower" | "higher";
}

/** A case of `test/cases`, as its program lists it (mcpp test spec B3): its place and what it states. */
export interface TestCase {
  name: string;
  line?: number;
  source: "listed";
  /** The file that declares it, in a program of several files. */
  file?: string;
  kind?: string;
  tags?: string[];
  expect?: string;
  bounds?: MetricBound[];
}

/**
 * `test/cases`: a test program's cases as it lists them (mcpp test spec B3). `protocol` is the version
 * of the test protocol it implements, or null: a program of none is one case. `framework` is the name
 * the project's framework states (B6), shown beside the program; nothing is decided by it.
 */
export interface TestCases {
  root: string;
  test: string;
  file: string;
  /** The files of a program of several files. */
  files?: string[];
  protocol?: number | null;
  framework: string | null;
  listed?: boolean;
  main?: { line: number; approximate?: boolean };
  cases: TestCase[];
}

/** A program of `test/list`: its files and the parts a run divides it into (design 2026-11-10). */
export interface TestProgram {
  test: string;
  member: string;
  main: string;
  files?: string[];
  parts?: { name: string; file: string }[];
  layout?: "file" | "directory";
}

/** A failure of a case (mcpp test spec O3): its place (0-based line), and when stated both sides, the statement and whether it ended the case. */
export interface CaseFailure {
  path: string;
  line: number;
  message: string;
  expected?: string;
  actual?: string;
  source?: string;
  fatal?: boolean;
}

/** A case as it ended, as the backend maps the test protocol's statuses. */
export interface CaseOutcome {
  case: string;
  status: "passed" | "failed" | "skipped" | "errored";
  durationMs?: number;
  reason?: string;
  message?: string;
  location?: FailureLocation | null;
  failures?: CaseFailure[];
  metrics?: Metric[];
  bounds?: MetricBound[];
  kind?: string;
  tags?: string[];
  expectedFailure?: boolean;
  unexpectedPass?: boolean;
  /** Of the checks the case ran, those that held and those that did not (O3). */
  checks?: { held: number; failed: number };
  /** What the case wrote (O8): its own output, as its program stated it. */
  output?: string;
}

/** `test/caseEvent`: a case began or ended while its program runs (test-case-stream, mcpp#917). */
export interface CaseEvent extends Partial<CaseOutcome> {
  jobId: string;
  root: string;
  program: string;
  part: string;
  case: string;
  event: "begin" | "end";
  tMs?: number;
}

/** `test/caseResult`: one case's outcome, from mcpp's records. */
/** Where a failure is: the first `file:line` its output names, the line 0-based. */
export interface FailureLocation {
  path: string;
  line: number;
}

export interface CaseResult extends CaseOutcome {
  jobId: string;
  root: string;
  test: string;
  program?: string;
  part?: string;
  case: string;
  status: "passed" | "failed" | "skipped" | "errored";
  durationMs: number;
  message: string;
  /** The case's own output (O8), or a failure's text; empty when neither. */
  output?: string;
  outputTruncated?: boolean;
  location?: FailureLocation | null;
}

/** One program's result, mcpp's status value for value (design 2026-11-07, D16). */
export interface TestResult {
  jobId: string;
  root: string;
  member: string;
  test: string;
  status: "passed" | "failed" | "timedOut" | "skipped" | "errored";
  timedOut?: boolean;
  compileFailed?: boolean;
  builtOnly?: boolean;
  durationMs: number;
  /** The program's whole output, compile then run, passed or not. */
  output: string;
  outputTruncated?: boolean;
  location?: FailureLocation | null;
  reason: string;
  /** The program the record belongs to, and the part of it the record is (design 2026-11-10). */
  program?: string;
  part?: string;
  /** A protocol program's cases, each as it ended. */
  cases?: CaseOutcome[];
}
