// Where the backend is, as the command that starts it (mcpp design 2026-10-28 part 2 §15):
//
//   1. mcpp.ide.backendPath: a program, or an ok-image bundle (`*.okb`) run by okrun;
//   2. the bundle the extension carries, `backend/mcpp-ide.okb`, run by the okrun it carries for
//      this system (`backend/okrun/<os>-<arch>/okrun[.exe]`), else by okrun on PATH -- one package
//      for every system and architecture;
//   3. a program the extension carries for this system (`bin/<platform>/mcpp-ide[.exe]`), the form
//      packages take until the bundle is built;
//   4. the mcpp-ide xlings installed (its command runs okrun on its bundle), then PATH.
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";

export interface Located {
  program: string;
  /** Arguments before the backend's own (`serve`): the bundle, when okrun runs one. */
  args: string[];
  source: "setting" | "bundle" | "bundled" | "xlings" | "PATH";
}

/** "linux-x64", "darwin-arm64", "win32-x64": the directory under bin/ for this system. */
export function platformDir(platform: NodeJS.Platform = process.platform, arch: string = process.arch): string {
  return `${platform}-${arch}`;
}

/** mcpp's names for a system and an architecture: "linux-x86_64", "macos-aarch64", "windows-x86_64". */
export function okrunDir(platform: NodeJS.Platform = process.platform, arch: string = process.arch): string {
  const system = platform === "win32" ? "windows" : platform === "darwin" ? "macos" : platform;
  const machine = arch === "x64" ? "x86_64" : arch === "arm64" ? "aarch64" : arch;
  return `${system}-${machine}`;
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

// A package's archive need not keep the mode bits.
function runnable(p: string, platform: NodeJS.Platform): string {
  if (platform !== "win32") {
    try { fs.accessSync(p, fs.constants.X_OK); } catch { try { fs.chmodSync(p, 0o755); } catch { /* reported when it is started */ } }
  }
  return p;
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
  const arch = options.arch ?? process.arch;
  const env = options.env ?? process.env;
  const bundledOkrun = path.join(options.extensionRoot, "backend", "okrun", okrunDir(platform, arch), executable("okrun", platform));
  const okrun = (): string | undefined => (isFile(bundledOkrun) ? runnable(bundledOkrun, platform) : onPath("okrun", env, platform));

  const configured = options.configured?.trim();
  if (configured) {
    if (configured.endsWith(".okb")) {
      const runner = okrun();
      return runner ? { program: runner, args: [configured], source: "setting" } : undefined;
    }
    return { program: configured, args: [], source: "setting" };
  }
  const bundle = path.join(options.extensionRoot, "backend", "mcpp-ide.okb");
  if (isFile(bundle)) {
    const runner = okrun();
    if (runner) return { program: runner, args: [bundle], source: "bundle" };
  }
  const bundled = path.join(options.extensionRoot, "bin", platformDir(platform, arch), executable("mcpp-ide", platform));
  if (isFile(bundled)) return { program: runnable(bundled, platform), args: [], source: "bundled" };
  const exe = executable("mcpp-ide", platform);
  const home = options.home ?? os.homedir();
  for (const candidate of [
    env.XLINGS_HOME ? path.join(env.XLINGS_HOME, "subos", "current", "bin", exe) : undefined,
    path.join(home, ".xlings", "subos", "current", "bin", exe),
    path.join(home, ".xlings", "subos", "default", "bin", exe),
  ]) {
    if (candidate && isFile(candidate)) return { program: candidate, args: [], source: "xlings" };
  }
  const found = onPath("mcpp-ide", env, platform);
  return found ? { program: found, args: [], source: "PATH" } : undefined;
}
