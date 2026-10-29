/**
 * `node:fs` for the page code: the IDE reads the files. A synchronous call is answered through the
 * bridge's synchronous channel, a promise through its asynchronous one; a directory listing brings
 * each entry's status along, so the `lstatSync` that follows a `readdirSync` (the walk of target/)
 * costs nothing more.
 */
import { request, requestSync } from "../bridge";

interface StatJson {
  type: "file" | "directory" | "symlink" | "other";
  size: number;
  mtimeMs: number;
  ino: number;
}

interface EntryJson extends StatJson {
  name: string;
}

class Stats {
  readonly size: number;
  readonly mtimeMs: number;
  readonly mtime: Date;
  readonly ino: number | bigint;
  readonly mtimeNs: bigint | number;
  constructor(private readonly s: StatJson, bigint = false) {
    this.size = s.size;
    this.mtimeMs = s.mtimeMs;
    this.mtime = new Date(s.mtimeMs);
    this.ino = bigint ? BigInt(s.ino) : s.ino;
    this.mtimeNs = bigint ? BigInt(Math.round(s.mtimeMs)) * 1000000n : s.mtimeMs * 1e6;
  }
  isFile(): boolean { return this.s.type === "file"; }
  isDirectory(): boolean { return this.s.type === "directory"; }
  isSymbolicLink(): boolean { return this.s.type === "symlink"; }
}

class Dirent {
  constructor(readonly name: string, private readonly type: StatJson["type"]) {}
  isFile(): boolean { return this.type === "file"; }
  isDirectory(): boolean { return this.type === "directory"; }
  isSymbolicLink(): boolean { return this.type === "symlink"; }
}

/** The statuses a listing brought, by full name: `lstatSync` reads them first. */
const listed = new Map<string, StatJson>();

function enoent(path: string): Error {
  return Object.assign(new Error(`ENOENT: no such file or directory, '${path}'`), { code: "ENOENT", path });
}

function childName(dir: string, name: string): string {
  return /[\\/]$/.test(dir) ? `${dir}${name}` : `${dir}${dir.includes("\\") && !dir.includes("/") ? "\\" : "/"}${name}`;
}

export function existsSync(path: string): boolean {
  try { return requestSync<StatJson | null>("fs.stat", { path, follow: true }) !== null; } catch { return false; }
}

export function statSync(path: string, options?: { bigint?: boolean }): Stats {
  const s = requestSync<StatJson | null>("fs.stat", { path, follow: true });
  if (s === null) throw enoent(path);
  return new Stats(s, options?.bigint === true);
}

export function lstatSync(path: string, options?: { bigint?: boolean }): Stats {
  const known = listed.get(path);
  if (known) return new Stats(known, options?.bigint === true);
  const s = requestSync<StatJson | null>("fs.stat", { path, follow: false });
  if (s === null) throw enoent(path);
  return new Stats(s, options?.bigint === true);
}

export function readFileSync(path: string, _encoding?: unknown): string {
  const text = requestSync<string | null>("fs.read", { path });
  if (text === null) throw enoent(path);
  return text;
}

export function readdirSync(path: string, options?: { withFileTypes?: boolean }): Array<Dirent | string> {
  const entries = requestSync<EntryJson[] | null>("fs.list", { path });
  if (entries === null) throw enoent(path);
  for (const e of entries) listed.set(childName(path, e.name), e);
  return options?.withFileTypes ? entries.map((e) => new Dirent(e.name, e.type)) : entries.map((e) => e.name);
}

export const promises = {
  async readFile(path: string, _encoding?: unknown): Promise<string> {
    const text = await request<string | null>("fs.read", { path });
    if (text === null) throw enoent(path);
    return text;
  },
  async writeFile(path: string, data: string | Uint8Array): Promise<void> {
    const base64 = typeof data !== "string";
    const text = typeof data === "string" ? data : btoa(String.fromCharCode(...data));
    await request("fs.write", { path, text, base64 });
  },
  async readdir(path: string, options?: { withFileTypes?: boolean }): Promise<Array<Dirent | string>> {
    const entries = await request<EntryJson[] | null>("fs.list", { path });
    if (entries === null) throw enoent(path);
    return options?.withFileTypes ? entries.map((e) => new Dirent(e.name, e.type)) : entries.map((e) => e.name);
  },
  async stat(path: string): Promise<Stats> {
    const s = await request<StatJson | null>("fs.stat", { path, follow: true });
    if (s === null) throw enoent(path);
    return new Stats(s);
  },
  async access(path: string): Promise<void> {
    if ((await request<StatJson | null>("fs.stat", { path, follow: true })) === null) throw enoent(path);
  },
  async realpath(path: string): Promise<string> {
    const real = await request<string | null>("fs.realpath", { path });
    if (real === null) throw enoent(path);
    return real;
  },
};

export default { existsSync, statSync, lstatSync, readFileSync, readdirSync, promises };
