// A job of the backend, followed from the moment it is asked for: its notifications can arrive
// before the reply that names its id (the job's thread and the reply race), so everything with an
// id not yet claimed is kept briefly and replayed to the handle that claims it.
import * as vscode from "vscode";

import type { JobFinished } from "./protocol";
import type { Session } from "./session";

const JOB_EVENTS = ["job/started", "job/output", "job/plan", "job/progress", "job/diagnostic", "job/artifact", "job/record", "job/finished", "test/result", "test/summary"];

export interface JobEvents {
  output?(text: string): void;
  started?(p: { argv: string[] }): void;
  plan?(p: any): void;
  progress?(p: { finished: number; total: number }): void;
  testResult?(p: any): void;
  testSummary?(p: any): void;
  finished?(p: JobFinished): void;
}

export class JobHandle {
  readonly done: Promise<JobFinished>;
  private resolveDone!: (p: JobFinished) => void;
  constructor(readonly id: string, private readonly session: Session) {
    this.done = new Promise((r) => { this.resolveDone = r; });
  }
  cancel(): Promise<unknown> {
    return this.session.request("job/cancel", { jobId: this.id });
  }
  /** @internal */
  finish(p: JobFinished): void {
    this.resolveDone(p);
  }
}

export class Jobs implements vscode.Disposable {
  private readonly unclaimed = new Map<string, { method: string; params: any }[]>();
  private readonly claimed = new Map<string, { events: JobEvents; handle: JobHandle }>();
  private readonly running = new Set<string>();
  private readonly disposables: vscode.Disposable[] = [];
  readonly onDidChangeRunning = new vscode.EventEmitter<number>();

  constructor(private readonly session: Session) {
    for (const method of JOB_EVENTS)
      this.disposables.push(session.on(method, (params: any) => this.route(method, params)));
    this.disposables.push(this.onDidChangeRunning);
  }

  get runningCount(): number {
    return this.running.size;
  }

  get runningIds(): string[] {
    return [...this.running];
  }

  private route(method: string, params: any): void {
    const id: string | undefined = params?.jobId;
    if (!id) return;
    if (method === "job/started") { this.running.add(id); this.onDidChangeRunning.fire(this.running.size); }
    if (method === "job/finished") { this.running.delete(id); this.onDidChangeRunning.fire(this.running.size); }
    const owner = this.claimed.get(id);
    if (!owner) {
      const list = this.unclaimed.get(id) ?? [];
      list.push({ method, params });
      this.unclaimed.set(id, list);
      // A job nobody claims (started by a panel) is kept only until it ends.
      if (method === "job/finished") setTimeout(() => this.unclaimed.delete(id), 30_000);
      return;
    }
    this.deliver(owner, method, params);
  }

  private deliver(owner: { events: JobEvents; handle: JobHandle }, method: string, params: any): void {
    const e = owner.events;
    switch (method) {
      case "job/output": e.output?.(params.text); break;
      case "job/started": e.started?.(params); break;
      case "job/plan": e.plan?.(params); break;
      case "job/progress": e.progress?.(params); break;
      case "test/result": e.testResult?.(params); break;
      case "test/summary": e.testSummary?.(params); break;
      case "job/finished":
        e.finished?.(params);
        owner.handle.finish(params);
        this.claimed.delete(params.jobId);
        break;
    }
  }

  /** Asks for a job (`job/start`, or another method that answers a job id) and follows it. */
  async start(params: Record<string, unknown>, events: JobEvents, method = "job/start"): Promise<JobHandle> {
    const { jobId } = await this.session.request<{ jobId: string }>(method, params);
    const handle = new JobHandle(jobId, this.session);
    const owner = { events, handle };
    this.claimed.set(jobId, owner);
    for (const m of this.unclaimed.get(jobId) ?? []) this.deliver(owner, m.method, m.params);
    this.unclaimed.delete(jobId);
    return handle;
  }

  dispose(): void {
    this.disposables.forEach((d) => d.dispose());
  }
}
