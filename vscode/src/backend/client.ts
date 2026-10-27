// The backend's wire, as the adapter speaks it: `mcpp-ide serve` started as a child process,
// JSON-RPC 2.0 framed as the LSP base protocol on its standard streams. No editor API here, so the
// unit tests and the panel tests drive a real backend through the same code.
import { spawn, type ChildProcess } from "node:child_process";
import { EventEmitter } from "node:events";

export interface RpcError {
  code: number;
  message: string;
  data?: unknown;
}

export class BackendError extends Error {
  constructor(public readonly method: string, public readonly error: RpcError) {
    super(error.message);
  }
}

type Pending = { resolve: (v: unknown) => void; reject: (e: unknown) => void; method: string };

/** Splits a byte stream into the bodies of Content-Length frames. */
export class FrameReader {
  private buffer = Buffer.alloc(0);

  feed(chunk: Buffer): string[] {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    const out: string[] = [];
    for (;;) {
      const headerEnd = this.buffer.indexOf("\r\n\r\n");
      if (headerEnd < 0) break;
      const headers = this.buffer.subarray(0, headerEnd).toString("ascii");
      const match = /content-length:\s*(\d+)/i.exec(headers);
      if (!match) {
        this.buffer = this.buffer.subarray(headerEnd + 4);
        continue;
      }
      const length = Number(match[1]);
      if (this.buffer.length < headerEnd + 4 + length) break;
      out.push(this.buffer.subarray(headerEnd + 4, headerEnd + 4 + length).toString("utf8"));
      this.buffer = this.buffer.subarray(headerEnd + 4 + length);
    }
    return out;
  }
}

export function encode(message: unknown): Buffer {
  const body = Buffer.from(JSON.stringify(message), "utf8");
  return Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, "ascii"), body]);
}

export interface BackendOptions {
  program: string;
  args?: string[];
  cwd?: string;
  env?: NodeJS.ProcessEnv;
  /** Lines the backend writes to its standard error (its log). */
  log?: (line: string) => void;
}

/**
 * One backend process. `request` sends a method and resolves with its result; `notifications`
 * emits each notification by method name (and "*" with the method first). `exited` resolves when
 * the process ends.
 */
export class Backend {
  readonly notifications = new EventEmitter();
  readonly exited: Promise<number | null>;
  private readonly child: ChildProcess;
  private readonly pending = new Map<number, Pending>();
  private readonly reader = new FrameReader();
  private nextId = 1;
  private alive = true;

  constructor(options: BackendOptions) {
    this.notifications.setMaxListeners(0);
    this.child = spawn(options.program, options.args ?? ["serve"], {
      cwd: options.cwd,
      env: options.env ?? process.env,
      stdio: ["pipe", "pipe", "pipe"],
      windowsHide: true,
    });
    this.child.stdout!.on("data", (chunk: Buffer) => {
      for (const body of this.reader.feed(chunk)) this.dispatch(body);
    });
    let errText = "";
    this.child.stderr!.on("data", (chunk: Buffer) => {
      errText += chunk.toString("utf8");
      let nl: number;
      while ((nl = errText.indexOf("\n")) >= 0) {
        options.log?.(errText.slice(0, nl));
        errText = errText.slice(nl + 1);
      }
    });
    this.exited = new Promise((resolve) => {
      const done = (code: number | null) => {
        if (!this.alive) return;
        this.alive = false;
        for (const p of this.pending.values()) p.reject(new BackendError(p.method, { code: -32099, message: "the mcpp-ide backend ended" }));
        this.pending.clear();
        resolve(code);
      };
      this.child.on("exit", (code) => done(code));
      this.child.on("error", (e) => {
        options.log?.(`mcpp-ide could not start: ${e.message}`);
        done(null);
      });
    });
  }

  get running(): boolean {
    return this.alive;
  }

  request<T = unknown>(method: string, params: unknown = {}): Promise<T> {
    if (!this.alive) return Promise.reject(new BackendError(method, { code: -32099, message: "the mcpp-ide backend is not running" }));
    const id = this.nextId++;
    return new Promise<T>((resolve, reject) => {
      this.pending.set(id, { resolve: resolve as (v: unknown) => void, reject, method });
      this.child.stdin!.write(encode({ jsonrpc: "2.0", id, method, params }));
    });
  }

  notify(method: string, params: unknown = {}): void {
    if (this.alive) this.child.stdin!.write(encode({ jsonrpc: "2.0", method, params }));
  }

  cancel(id: number): void {
    this.notify("$/cancelRequest", { id });
  }

  async stop(): Promise<void> {
    if (!this.alive) return;
    try {
      await Promise.race([this.request("shutdown"), new Promise((r) => setTimeout(r, 2000))]);
    } catch {
      // ended already
    }
    this.notify("exit");
    this.child.stdin!.end();
    const ended = await Promise.race([this.exited.then(() => true), new Promise<boolean>((r) => setTimeout(() => r(false), 3000))]);
    if (!ended) this.child.kill();
  }

  private dispatch(body: string): void {
    let message: { id?: number; method?: string; params?: unknown; result?: unknown; error?: RpcError };
    try {
      message = JSON.parse(body);
    } catch {
      return;
    }
    if (message.id !== undefined && message.id !== null && (message.result !== undefined || message.error !== undefined)) {
      const p = this.pending.get(message.id);
      if (!p) return;
      this.pending.delete(message.id);
      if (message.error) p.reject(new BackendError(p.method, message.error));
      else p.resolve(message.result);
      return;
    }
    if (message.method) {
      this.notifications.emit(message.method, message.params);
      this.notifications.emit("*", message.method, message.params);
    }
  }
}
