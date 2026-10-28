// Where the mcppls VS Code extension keeps its server: `<extension>/payload/<payload.json's
// server.path>`, or MCPPLS_SERVER. The backend reads its version (languageService/status).
import * as fs from "node:fs";
import * as path from "node:path";

export function mcpplsCandidates(extensionPath: string | undefined, env: NodeJS.ProcessEnv = process.env): string[] {
  const out: string[] = [];
  if (env.MCPPLS_SERVER) out.push(env.MCPPLS_SERVER);
  if (!extensionPath) return out;
  const payload = env.MCPPLS_PAYLOAD ? path.resolve(env.MCPPLS_PAYLOAD) : path.join(extensionPath, "payload");
  try {
    const manifest = JSON.parse(fs.readFileSync(path.join(payload, "payload.json"), "utf8")) as { server?: { path?: string } };
    if (manifest.server?.path) out.push(path.join(payload, manifest.server.path));
  } catch {
    // An installation without a payload answers nothing here; PATH and xlings are tried after.
  }
  return out;
}
