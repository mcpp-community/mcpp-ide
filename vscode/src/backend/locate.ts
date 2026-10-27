// Where the backend is: the configured path, the one the extension carries for this system
// (`bin/<platform>/mcpp-ide[.exe]`), the one xlings installed, then PATH.
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

export interface Located {
  program: string;
  source: "setting" | "bundled" | "xlings" | "PATH";
}

/** "linux-x64", "darwin-arm64", "win32-x64": the directory under bin/ for this system. */
export function platformDir(platform: NodeJS.Platform = process.platform, arch: string = process.arch): string {
  return `${platform}-${arch}`;
}

function executable(name: string, platform: NodeJS.Platform): string {
  return platform === "win32" ? `${name}.exe` : name;
}

function isFile(p: string): boolean {
  try {
    return fs.statSync(p).isFile();
  } catch {
    return false;
  }
}

export function onPath(name: string, env: NodeJS.ProcessEnv = process.env, platform: NodeJS.Platform = process.platform): string | undefined {
  const dirs = (env.PATH ?? env.Path ?? "").split(platform === "win32" ? ";" : ":").filter(Boolean);
  for (const d of dirs) {
    const candidate = path.join(d, executable(name, platform));
    if (isFile(candidate)) return candidate;
  }
  return undefined;
}

export function locateBackend(options: {
  configured?: string;
  extensionRoot: string;
  env?: NodeJS.ProcessEnv;
  platform?: NodeJS.Platform;
  arch?: string;
  home?: string;
}): Located | undefined {
  const platform = options.platform ?? process.platform;
  const env = options.env ?? process.env;
  const exe = executable("mcpp-ide", platform);
  if (options.configured && options.configured.trim()) {
    return { program: options.configured.trim(), source: "setting" };
  }
  const bundled = path.join(options.extensionRoot, "bin", platformDir(platform, options.arch ?? process.arch), exe);
  if (isFile(bundled)) {
    // A package's archive need not keep the mode bits.
    if (platform !== "win32") {
      try { fs.accessSync(bundled, fs.constants.X_OK); } catch { try { fs.chmodSync(bundled, 0o755); } catch { /* reported when it is started */ } }
    }
    return { program: bundled, source: "bundled" };
  }
  const home = options.home ?? os.homedir();
  const xlingsHome = env.XLINGS_HOME;
  for (const candidate of [
    xlingsHome ? path.join(xlingsHome, "subos", "current", "bin", exe) : undefined,
    path.join(home, ".xlings", "subos", "current", "bin", exe),
    path.join(home, ".xlings", "subos", "default", "bin", exe),
  ]) {
    if (candidate && isFile(candidate)) return { program: candidate, source: "xlings" };
  }
  const found = onPath("mcpp-ide", env, platform);
  return found ? { program: found, source: "PATH" } : undefined;
}
