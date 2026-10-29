/** The `process` global the page code reads: the IDE's system and environment. */
import { info } from "../bridge";

export const process = {
  get platform(): string { return info.platform; },
  get arch(): string { return info.arch; },
  get env(): Record<string, string> { return info.env; },
  cwd: (): string => info.workspaceFolders[0] ?? info.home,
  versions: {} as Record<string, string>,
};
