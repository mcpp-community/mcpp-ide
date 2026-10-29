// What the unit tests share: the extension's directory (mide-plugins/vscode), the repository, the
// programs its build produced (the backend and the mock mcpp, newest first), and a project directory
// the mock answers for.
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

// Compiled to out/test/unit.
export const extensionDir = path.resolve(__dirname, "..", "..", "..");
export const repo = path.resolve(extensionDir, "..", "..");

export function built(name: string): string | undefined {
  const exe = process.platform === "win32" ? `${name}.exe` : name;
  if (name === "mcpp-ide" && process.env.MCPP_IDE_BACKEND) return process.env.MCPP_IDE_BACKEND;
  let best: string | undefined;
  let bestTime = 0;
  const walk = (dir: string, depth: number) => {
    if (depth > 4) return;
    let entries: fs.Dirent[] = [];
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
    for (const e of entries) {
      const p = path.join(dir, e.name);
      if (e.isDirectory()) walk(p, depth + 1);
      else if (e.name === exe && path.basename(dir) === "bin") {
        const t = fs.statSync(p).mtimeMs;
        if (t > bestTime) { best = p; bestTime = t; }
      }
    }
  };
  walk(path.join(repo, "target"), 0);
  return best;
}

export function project(): string {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "mcppide-vscode-"));
  fs.mkdirSync(path.join(root, "src"));
  fs.writeFileSync(path.join(root, "mcpp.toml"), '[package]\nname = "app"\nversion = "0.1.0"\n');
  return root;
}
