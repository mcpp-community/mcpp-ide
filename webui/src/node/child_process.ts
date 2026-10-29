/**
 * `node:child_process` for the page code. The workbench starts no program itself: mcpp and mcppls
 * run through the backend (the process module's executors), and anything else is refused.
 */
export function execFile(file: string, _args: unknown, _options: unknown, callback?: (error: Error) => void): void {
  const done = typeof _options === "function" ? (_options as (error: Error) => void) : callback;
  done?.(new Error(`the workbench does not start programs (${file})`));
}
export default { execFile };
