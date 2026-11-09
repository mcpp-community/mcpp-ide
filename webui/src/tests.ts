// The tests page: what the last runs of a project's tests say (the backend's `mcpp-ide/test/insights`,
// read from the store mcpp keeps; design 2026-11-10). mcpp-clion shows it in JCEF beside the graph
// page, mcpp-vscode in a webview. The page decides nothing: the host hands it the backend's answer and
// its strings (`mcppRender`), and a click on a failure asks the host to open its place
// (`mcppHostSend`). A section whose data the store does not hold is left out; the note says why.

export interface Labels {
  title: string;
  /** Said when the store holds no run; `{0}` is the store's directory. */
  noRun: string;
  /** `{0}` runs kept, `{1}` the run's start. */
  window: string;
  failures: string;
  /** A failure that began in the last run. */
  fresh: string;
  /** `{0}`: the run since which a case fails. */
  since: string;
  expected: string;
  actual: string;
  metrics: string;
  slowest: string;
  /** `{0}`: a duration. */
  median: string;
  flaky: string;
  /** `{0}` passes, `{1}` failures, `{2}` changes. */
  flakyCounts: string;
  schedule: string;
  /** `{0}`: a worker's number. */
  worker: string;
  criticalPath: string;
  /** Said when mcpp states no metrics (test-metrics, mcpp#917). */
  noMetrics: string;
}

export interface Bound { max?: number; min?: number }

export interface Insights {
  store: string;
  runs: number;
  run: { id: string; started?: string; status: string; ms?: number; jobs?: number; counts?: Record<string, number> } | null;
  byKind?: Record<string, Record<string, number>>;
  failures: { program: string; part?: string; case: string; status: string; message?: string; path?: string; line?: number;
              expected?: string; actual?: string; since: string; new: boolean }[];
  slowest: { program: string; part?: string; case: string; ms: number; medianMs: number }[];
  flaky: { program: string; part?: string; case: string; passed: number; failed: number; flips: number }[];
  metrics: { program: string; part?: string; case: string; name: string; unit?: string; values: { run: string; value: number }[];
             last: number; bound: Bound | null; margin: number | null; better?: string }[];
  schedule: { lanes: { worker: number; items: { program: string; part?: string; startMs: number; ms: number; status: string }[] }[];
              criticalPath: string[]; ms?: number } | null;
  available?: { kinds?: boolean; metrics?: boolean; cases?: boolean };
  features?: { parts?: boolean; live?: boolean; metrics?: boolean };
}

export interface TestsPage {
  insights: Insights;
  labels: Labels;
  dark?: boolean;
}

declare global {
  interface Window {
    mcppRender(page: TestsPage): void;
    mcppHostSend?: (text: string) => void;
  }
}

const STYLE = `
:root { --fg: #1f1f1f; --bg: #ffffff; --muted: #6b6f76; --line: #d9dbe0; --ok: #2e7d32; --bad: #c62828; --warn: #b26a00; --accent: #3367d6; }
body.dark { --fg: #dfe1e5; --bg: #2b2d30; --muted: #9a9ea6; --line: #43454a; --ok: #6abf69; --bad: #ef6c6c; --warn: #e0a54a; --accent: #7aa2f7; }
body.vscode { --fg: var(--vscode-foreground); --bg: var(--vscode-editor-background); --muted: var(--vscode-descriptionForeground);
  --line: var(--vscode-panel-border, var(--vscode-widget-border, #8884)); --ok: var(--vscode-testing-iconPassed);
  --bad: var(--vscode-testing-iconFailed); --warn: var(--vscode-editorWarning-foreground); --accent: var(--vscode-textLink-foreground); }
body { margin: 0; padding: 12px 16px; font: 13px system-ui, sans-serif; color: var(--fg); background: var(--bg); }
h1 { font-size: 15px; margin: 0 0 2px; }
h2 { font-size: 12px; text-transform: uppercase; letter-spacing: .04em; color: var(--muted); margin: 18px 0 6px; font-weight: 600; }
.sub { color: var(--muted); margin-bottom: 8px; }
.counts span { margin-right: 12px; font-variant-numeric: tabular-nums; }
.ok { color: var(--ok); } .bad { color: var(--bad); } .warn { color: var(--warn); } .muted { color: var(--muted); }
.chip { display: inline-block; border: 1px solid var(--line); border-radius: 10px; padding: 0 8px; margin: 4px 6px 0 0; font-size: 12px; }
.row { display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 8px; padding: 4px 0; border-top: 1px solid var(--line); align-items: center; }
.row:first-of-type { border-top: 0; }
.name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.name small { color: var(--muted); }
a[data-open] { color: inherit; text-decoration: none; cursor: pointer; }
a[data-open]:hover .case { text-decoration: underline; }
.detail { grid-column: 1 / -1; color: var(--muted); font: 12px ui-monospace, monospace; white-space: pre-wrap; }
.badge { font-size: 11px; border-radius: 3px; padding: 0 5px; border: 1px solid currentColor; }
.bar { height: 4px; background: var(--accent); border-radius: 2px; opacity: .7; }
svg text { fill: var(--muted); font-size: 10px; }
.spark polyline { fill: none; stroke: var(--accent); stroke-width: 1.5; }
.spark line { stroke: var(--bad); stroke-dasharray: 3 2; stroke-width: 1; }
.lane rect { fill: var(--accent); opacity: .55; }
.lane rect.fail { fill: var(--bad); opacity: .7; }
.lane rect.critical { opacity: 1; stroke: var(--fg); stroke-width: 1; }
.note { color: var(--muted); margin-top: 16px; font-size: 12px; }
`;

export function escapeHtml(s: string): string {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
}

function fill(text: string, ...args: (string | number)[]): string {
  return text.replace(/\{(\d)\}/g, (_, i: string) => String(args[Number(i)] ?? ""));
}

/** A duration as a person reads it: `0.4 ms`, `12 ms`, `3.4 s`, `2 min 5 s`. */
export function duration(ms: number): string {
  if (ms < 1) return `${ms.toFixed(1)} ms`;
  if (ms < 1000) return `${Math.round(ms)} ms`;
  if (ms < 60_000) return `${(ms / 1000).toFixed(1)} s`;
  return `${Math.floor(ms / 60_000)} min ${Math.round((ms % 60_000) / 1000)} s`;
}

/** A metric's value with its unit: three significant digits. */
export function measured(value: number, unit?: string): string {
  const text = Math.abs(value) >= 100 ? value.toFixed(0) : Number(value.toPrecision(3)).toString();
  return unit && unit !== "1" ? `${text} ${unit}` : text;
}

/** `≤ 15 s`, `≥ 0.9`, `0.9 – 15 s`. */
export function boundText(b: Bound | null, unit?: string): string {
  if (!b) return "";
  if (b.max !== undefined && b.min !== undefined) return `${measured(b.min)} – ${measured(b.max, unit)}`;
  if (b.max !== undefined) return `≤ ${measured(b.max, unit)}`;
  if (b.min !== undefined) return `≥ ${measured(b.min, unit)}`;
  return "";
}

function where(row: { program: string; part?: string; case: string }): string {
  const part = row.part ? ` <small>${escapeHtml(row.part.slice(row.program.length + 1) || row.part)}</small>` : "";
  return `<span class="case">${escapeHtml(row.case || row.program)}</span> <small>${escapeHtml(row.case ? row.program : "")}</small>${part}`;
}

/** A series as a line 120×24, the bound dashed when it lies within reach of the values. */
export function sparkline(values: number[], bound: Bound | null): string {
  if (values.length === 0) return "";
  const limit = bound?.max ?? bound?.min;
  const all = limit !== undefined ? [...values, limit] : values;
  const lo = Math.min(...all), hi = Math.max(...all);
  const span = hi - lo || 1;
  const y = (v: number) => (22 - ((v - lo) / span) * 20).toFixed(1);
  const step = values.length > 1 ? 118 / (values.length - 1) : 0;
  const points = values.map((v, i) => `${(1 + i * step).toFixed(1)},${y(v)}`).join(" ");
  const line = limit !== undefined ? `<line x1="0" x2="120" y1="${y(limit)}" y2="${y(limit)}"/>` : "";
  return `<svg class="spark" width="120" height="24" viewBox="0 0 120 24" role="img">${line}<polyline points="${points}"/></svg>`;
}

function summary(page: TestsPage): string {
  const { insights: x, labels: l } = page;
  const run = x.run!;
  const c = run.counts ?? {};
  const n = (k: string) => c[k] ?? 0;
  const bad = n("fail") + n("xpass") + n("invalid") + n("build-fail");
  const counts = [`<span class="ok">✓ ${n("pass") + n("xfail")}</span>`];
  if (bad) counts.push(`<span class="bad">✗ ${bad}</span>`);
  if (n("skip") + n("not-run")) counts.push(`<span class="muted">○ ${n("skip") + n("not-run")}</span>`);
  if (run.ms) counts.push(`<span class="muted">${duration(run.ms)}</span>`);
  const kinds = Object.entries(x.byKind ?? {}).filter(([k]) => k).map(([k, v]) => {
    const total = Object.values(v).reduce((a, b) => a + b, 0);
    const failing = (v.fail ?? 0) + (v.xpass ?? 0) + (v.invalid ?? 0);
    return `<span class="chip">${escapeHtml(k)} ${total}${failing ? ` <span class="bad">✗ ${failing}</span>` : ""}</span>`;
  }).join("");
  return `<h1>${escapeHtml(l.title)}</h1><div class="sub">${escapeHtml(fill(l.window, x.runs, run.started ?? run.id))}</div>
<div class="counts">${counts.join("")}</div>${kinds ? `<div>${kinds}</div>` : ""}`;
}

function failures(page: TestsPage): string {
  const { insights: x, labels: l } = page;
  if (x.failures.length === 0) return "";
  const rows = x.failures.map((f) => {
    const open = f.path ? ` data-open="${escapeHtml(JSON.stringify({ path: f.path, line: f.line ?? 0 }))}"` : "";
    const badge = f.new ? `<span class="badge bad">${escapeHtml(l.fresh)}</span>` : `<span class="badge warn">${escapeHtml(fill(l.since, f.since))}</span>`;
    const sides = f.expected !== undefined ? `\n${l.expected}: ${f.expected}\n${l.actual}:   ${f.actual ?? ""}` : "";
    const detail = (f.message ?? "") + sides;
    return `<div class="row"><a class="name"${open}><span class="bad">✗</span> ${where(f)}</a>${badge}${detail ? `<div class="detail">${escapeHtml(detail)}</div>` : ""}</div>`;
  });
  return `<h2>${escapeHtml(l.failures)}</h2>${rows.join("")}`;
}

function metrics(page: TestsPage): string {
  const { insights: x, labels: l } = page;
  if (x.metrics.length === 0) return "";
  const rows = x.metrics.map((m) => {
    const tone = m.margin === null ? "" : m.margin < 0 ? "bad" : m.margin < 0.2 ? "warn" : "ok";
    const bound = boundText(m.bound, m.unit);
    const value = `<span class="${tone}">${escapeHtml(measured(m.last, m.unit))}</span>${bound ? ` <span class="muted">${escapeHtml(bound)}</span>` : ""}`;
    return `<div class="row"><span class="name">${escapeHtml(m.name)} · ${where(m)}</span><span>${sparkline(m.values.map((v) => v.value), m.bound)} ${value}</span></div>`;
  });
  return `<h2>${escapeHtml(l.metrics)}</h2>${rows.join("")}`;
}

function slowest(page: TestsPage): string {
  const { insights: x, labels: l } = page;
  if (x.slowest.length === 0) return "";
  const top = Math.max(...x.slowest.map((s) => s.ms), 1);
  const rows = x.slowest.map((s) => `<div class="row"><span class="name">${where(s)}<div class="bar" style="width:${Math.max(2, (s.ms / top) * 100).toFixed(0)}%"></div></span>
<span>${escapeHtml(duration(s.ms))} <span class="muted">${escapeHtml(fill(l.median, duration(s.medianMs)))}</span></span></div>`);
  return `<h2>${escapeHtml(l.slowest)}</h2>${rows.join("")}`;
}

function flaky(page: TestsPage): string {
  const { insights: x, labels: l } = page;
  if (x.flaky.length === 0) return "";
  const rows = x.flaky.map((f) => `<div class="row"><span class="name"><span class="warn">⟳</span> ${where(f)}</span>
<span class="muted">${escapeHtml(fill(l.flakyCounts, f.passed, f.failed, f.flips))}</span></div>`);
  return `<h2>${escapeHtml(l.flaky)}</h2>${rows.join("")}`;
}

/** The last run's schedule: a lane per worker, each process a bar, the critical path outlined. */
export function schedule(page: TestsPage): string {
  const { insights: x, labels: l } = page;
  const s = x.schedule;
  if (!s || s.lanes.length === 0) return "";
  const end = Math.max(s.ms ?? 0, ...s.lanes.flatMap((lane) => lane.items.map((i) => i.startMs + i.ms)), 1);
  const width = 600, row = 18;
  const critical = new Set(s.criticalPath);
  const lanes = s.lanes.map((lane, i) => {
    const bars = lane.items.map((item) => {
      const x0 = (item.startMs / end) * width, w = Math.max(1.5, (item.ms / end) * width);
      const cls = [item.status === "fail" || item.status === "build-fail" ? "fail" : "", critical.has(item.program) ? "critical" : ""].join(" ").trim();
      return `<rect x="${(x0 + 64).toFixed(1)}" y="${i * row + 3}" width="${w.toFixed(1)}" height="${row - 6}" rx="2"${cls ? ` class="${cls}"` : ""}><title>${escapeHtml(`${item.part ?? item.program} · ${duration(item.ms)}`)}</title></rect>`;
    }).join("");
    return `<g class="lane"><text x="0" y="${i * row + 13}">${escapeHtml(fill(l.worker, lane.worker))}</text>${bars}</g>`;
  }).join("");
  const height = s.lanes.length * row + 14;
  const axis = `<text x="64" y="${height - 2}">0</text><text x="${width + 64}" y="${height - 2}" text-anchor="end">${escapeHtml(duration(end))}</text>`;
  const path = s.criticalPath.length ? `<div class="sub">${escapeHtml(l.criticalPath)}: ${s.criticalPath.map(escapeHtml).join(" → ")}</div>` : "";
  return `<h2>${escapeHtml(l.schedule)}</h2><svg width="100%" viewBox="0 0 ${width + 66} ${height}" role="img">${lanes}${axis}</svg>${path}`;
}

/** The page's whole document body, from the backend's answer and the host's strings. */
export function renderTests(page: TestsPage): string {
  const { insights: x, labels: l } = page;
  if (!x.run) return `<h1>${escapeHtml(l.title)}</h1><p class="note">${escapeHtml(fill(l.noRun, x.store))}</p>`;
  const note = x.features?.metrics || x.available?.metrics ? "" : `<p class="note">${escapeHtml(l.noMetrics)}</p>`;
  return summary(page) + failures(page) + metrics(page) + slowest(page) + flaky(page) + schedule(page) + note;
}

if (typeof window !== "undefined") {
  window.mcppRender = (page: TestsPage): void => {
    if (!document.getElementById("mcpp-tests-style")) {
      const style = document.createElement("style");
      style.id = "mcpp-tests-style";
      style.textContent = STYLE;
      document.head.appendChild(style);
    }
    document.body.classList.toggle("dark", !!page.dark);
    const target = document.getElementById("tests") ?? document.body;
    target.innerHTML = renderTests(page);
    for (const a of Array.from(target.querySelectorAll<HTMLElement>("[data-open]")))
      a.addEventListener("click", () => window.mcppHostSend?.(JSON.stringify({ open: JSON.parse(a.dataset.open!) })));
  };
}
