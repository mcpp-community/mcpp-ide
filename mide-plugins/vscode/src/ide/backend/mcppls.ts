// Where the mcppls VS Code extension keeps its server: its payload directory (MCPPLS_PAYLOAD, else
// `<extension>/payload`), whose payload.json's `server` the backend starts -- the runner and the okb, or a
// program -- and MCPPLS_SERVER. The backend reads its version (languageService/status).
import * as fs from "node:fs";
import * as path from "node:path";

export function mcpplsCandidates(extensionPath: string | undefined, env: NodeJS.ProcessEnv = process.env): string[] {
  const out: string[] = [];
  if (env.MCPPLS_SERVER) out.push(env.MCPPLS_SERVER);
  if (!extensionPath) return out;
  const payload = env.MCPPLS_PAYLOAD ? path.resolve(env.MCPPLS_PAYLOAD) : path.join(extensionPath, "payload");
  // An installation without a payload answers nothing here; PATH and xlings are tried after.
  if (fs.existsSync(path.join(payload, "payload.json"))) out.push(payload);
  return out;
}
