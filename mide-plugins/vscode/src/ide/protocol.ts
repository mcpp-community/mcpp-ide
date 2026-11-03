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
}

/** `source/entries` (design 2026-10-30 part 2 §6.3): what a C++ file is to its project. */
export interface SourceEntries {
  file: string;
  root?: string;
  kind: "program" | "test" | "none";
  test?: string;
  framework?: string;
  main?: { line: number; approximate?: boolean };
}

/** `test/cases` (§7.3): a test program's cases, from its source and, once listed, from the program. */
export interface TestCases {
  root: string;
  test: string;
  file: string;
  framework: string | null;
  listed?: boolean;
  main?: { line: number; approximate?: boolean };
  cases: { name: string; line?: number; source: "static" | "listed" }[];
}

/** `test/caseResult`: one case's outcome, read from the framework's report. */
export interface CaseResult {
  jobId: string;
  root: string;
  test: string;
  case: string;
  status: "passed" | "failed" | "skipped";
  durationMs: number;
  message: string;
}

export interface TestResult {
  jobId: string;
  root: string;
  member: string;
  test: string;
  status: "passed" | "failed" | "timedOut" | "skipped" | "errored";
  durationMs: number;
  output: string;
  reason: string;
}
