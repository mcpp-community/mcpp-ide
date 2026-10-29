/** `node:os` for the page code: the IDE's machine. */
import { info } from "../bridge";

export function homedir(): string { return info.home; }
export function tmpdir(): string { return info.tmp; }
export function platform(): string { return info.platform; }
export function arch(): string { return info.arch; }
export const EOL = "\n";
export default { homedir, tmpdir, platform, arch, EOL };
