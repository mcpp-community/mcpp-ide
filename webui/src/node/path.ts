/**
 * `node:path` for the page code in the workbench: the conventions of the system the IDE runs on
 * (Windows names with `\` and a drive, POSIX names otherwise), as Node's own module answers them on
 * that system, for the functions the page code calls.
 */
import { info } from "../bridge";

const windows = (): boolean => info.platform === "win32";

function splitRoot(p: string): [root: string, rest: string] {
  if (!windows()) return p.startsWith("/") ? ["/", p.slice(1)] : ["", p];
  const drive = /^[A-Za-z]:/.exec(p);
  if (drive) {
    const after = p.slice(2);
    return /^[\\/]/.test(after) ? [`${drive[0]}\\`, after.slice(1)] : [drive[0], after];
  }
  const unc = /^[\\/]{2}[^\\/]+[\\/][^\\/]+/.exec(p);
  if (unc) return [`${unc[0].replace(/\//g, "\\")}\\`, p.slice(unc[0].length + 1)];
  return /^[\\/]/.test(p) ? ["\\", p.slice(1)] : ["", p];
}

function parts(rest: string): string[] {
  return rest.split(windows() ? /[\\/]+/ : /\/+/).filter((s) => s.length > 0);
}

export function normalize(p: string): string {
  if (p.length === 0) return ".";
  const [root, rest] = splitRoot(p);
  const out: string[] = [];
  for (const part of parts(rest)) {
    if (part === ".") continue;
    if (part === "..") {
      if (out.length > 0 && out[out.length - 1] !== "..") out.pop();
      else if (!root) out.push("..");
      continue;
    }
    out.push(part);
  }
  const trailing = /[\\/]$/.test(p) && out.length > 0 ? sep : "";
  const body = out.join(sep);
  if (!root) return (body || ".") + trailing;
  return root + body + trailing;
}

export function isAbsolute(p: string): boolean {
  if (!windows()) return p.startsWith("/");
  return /^[A-Za-z]:[\\/]/.test(p) || /^[\\/]/.test(p);
}

export function join(...segments: string[]): string {
  const joined = segments.filter((s) => s.length > 0).join(sep);
  return joined.length === 0 ? "." : normalize(joined);
}

export function resolve(...segments: string[]): string {
  let resolved = "";
  for (let i = segments.length - 1; i >= 0 && !isAbsolute(resolved); i--) {
    const s = segments[i];
    if (s.length === 0) continue;
    resolved = resolved ? `${s}${sep}${resolved}` : s;
  }
  if (!isAbsolute(resolved)) resolved = `${info.workspaceFolders[0] ?? info.home}${sep}${resolved}`;
  const n = normalize(resolved);
  return n.length > 1 && /[\\/]$/.test(n) && !/^[A-Za-z]:\\$/.test(n) ? n.slice(0, -1) : n;
}

export function relative(from: string, to: string): string {
  const a = resolve(from), b = resolve(to);
  const fold = (s: string) => (windows() ? s.toLowerCase() : s);
  const [ra, xa] = splitRoot(a), [rb, xb] = splitRoot(b);
  if (fold(ra) !== fold(rb)) return b;
  const pa = parts(xa), pb = parts(xb);
  let i = 0;
  while (i < pa.length && i < pb.length && fold(pa[i]) === fold(pb[i])) i++;
  return [...pa.slice(i).map(() => ".."), ...pb.slice(i)].join(sep);
}

export function dirname(p: string): string {
  const [root, rest] = splitRoot(p);
  const ps = parts(rest);
  if (ps.length <= 1) return root || ".";
  return root + ps.slice(0, -1).join(sep);
}

export function basename(p: string, ext?: string): string {
  const [, rest] = splitRoot(p);
  const ps = parts(rest);
  const base = ps.length > 0 ? ps[ps.length - 1] : "";
  return ext && base.endsWith(ext) && base !== ext ? base.slice(0, -ext.length) : base;
}

export function extname(p: string): string {
  const base = basename(p);
  const dot = base.lastIndexOf(".");
  return dot <= 0 ? "" : base.slice(dot);
}

export let sep = "/";
export let delimiter = ":";

/** Called once the IDE has said which system it runs on. */
export function configure(): void {
  sep = windows() ? "\\" : "/";
  delimiter = windows() ? ";" : ":";
  api.sep = sep;
  api.delimiter = delimiter;
}

const api = { normalize, isAbsolute, join, resolve, relative, dirname, basename, extname, sep, delimiter };
export default api;
