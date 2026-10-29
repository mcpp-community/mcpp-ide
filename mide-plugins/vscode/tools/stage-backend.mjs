/**
 * The backend the package carries (mcpp design 2026-10-28 part 2 §15): the ok-image bundle as
 * backend/mcpp-ide.okb, one for every system, and every system's runner as
 * backend/okrun/<os>-<arch>/okrun[.exe]. Taken from MCPP_IDE_OKB (a file) and MCPP_IDE_OKRUN (a
 * directory laid out <os>-<arch>/), which CI's backend job produces; without them there is nothing
 * to package.
 */
import fs from "node:fs";
import path from "node:path";

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), "..");
const okb = process.env.MCPP_IDE_OKB;
const okrun = process.env.MCPP_IDE_OKRUN;
if (!okb || !fs.statSync(okb, { throwIfNoEntry: false })?.isFile()) {
  console.error("stage-backend: MCPP_IDE_OKB names no file (the backend's ok-image bundle)");
  process.exit(1);
}
if (!okrun || !fs.statSync(okrun, { throwIfNoEntry: false })?.isDirectory()) {
  console.error("stage-backend: MCPP_IDE_OKRUN names no directory (okrun for each <os>-<arch>)");
  process.exit(1);
}
const out = path.join(root, "backend");
fs.rmSync(out, { recursive: true, force: true });
fs.mkdirSync(out, { recursive: true });
fs.copyFileSync(okb, path.join(out, "mcpp-ide.okb"));
fs.cpSync(okrun, path.join(out, "okrun"), { recursive: true });
for (const dir of fs.readdirSync(path.join(out, "okrun"))) {
  for (const file of fs.readdirSync(path.join(out, "okrun", dir))) fs.chmodSync(path.join(out, "okrun", dir, file), 0o755);
}
console.log(`stage-backend: ${path.relative(root, out)}/mcpp-ide.okb and okrun for ${fs.readdirSync(path.join(out, "okrun")).join(", ")}`);
