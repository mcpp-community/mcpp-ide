/** `node:url` for the page code: file URLs. */
import { info } from "../bridge";

export function fileURLToPath(url: string | URL): string {
  const u = typeof url === "string" ? new URL(url) : url;
  const p = decodeURIComponent(u.pathname);
  return info.platform === "win32" ? p.replace(/^\/([A-Za-z]:)/, "$1").replace(/\//g, "\\") : p;
}

export function pathToFileURL(p: string): URL {
  const forward = p.replace(/\\/g, "/");
  return new URL(`file://${forward.startsWith("/") ? "" : "/"}${encodeURI(forward)}`);
}

export default { fileURLToPath, pathToFileURL };
