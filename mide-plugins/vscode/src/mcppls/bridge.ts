/**
 * The one place this extension talks to the C++ Modules extension.
 *
 * It forwards mcppls's actions and nothing else: no LSP client, no reading of
 * `mcppls.*` settings, no writing of them, no parsing of its logs. mcppls says
 * which actions exist and which command each one is (the backend's
 * `languageService/status`); the capability table (`./contract.ts`) says how
 * much confirmation each needs; `./capabilities.ts` puts the two together.
 *
 * Results are **structured, not phrased**: a caller that has a user in front of
 * it turns them into words through `./messages.ts`. That keeps this module free
 * of `vscode` and therefore unit-testable, and it keeps the strings translatable
 * without threading a translator through the probe logic.
 */

import { CapabilityRegistry, type CapabilityEnvironment, type InvokeResult } from "./capabilities";
import { capability } from "./contract";

export { MCPPLS_EXTENSION_ID } from "./contract";

export type LanguageServerCommandState = InvokeResult["state"];
export type LanguageServerCommandResult = InvokeResult;
export { CapabilityRegistry } from "./capabilities";
export type { CapabilityEnvironment } from "./capabilities";

export interface LanguageServerBridge {
  /** The probe, exposed so the environment self-check can print the whole table. */
  readonly capabilities: CapabilityRegistry;
  /** Generic entry point for the view and the menu. */
  invoke(key: string, ...args: unknown[]): Promise<LanguageServerCommandResult>;
  /** True when the user should not be offered this capability. */
  isGone(key: string): boolean;
  /** True when mcppls has not answered yet. */
  isUnconfirmed(key: string): boolean;
  /** How much confirmation the action needs. */
  dangerOf(key: string): "none" | "confirm" | "destructive";

  refreshLanguageServerAfterBuild(): Promise<LanguageServerCommandResult>;
  restartLanguageServer(): Promise<LanguageServerCommandResult>;
  restartEngine(): Promise<LanguageServerCommandResult>;
  resetWorkspaceCache(): Promise<LanguageServerCommandResult>;
  selectContext(): Promise<LanguageServerCommandResult>;
  showModuleGraph(): Promise<LanguageServerCommandResult>;
  showLanguageServerLogs(): Promise<LanguageServerCommandResult>;
  collectReport(): Promise<LanguageServerCommandResult>;
  exportDiagnosticBundle(): Promise<LanguageServerCommandResult>;
  runBuildToolInTerminal(): Promise<LanguageServerCommandResult>;
  manageConflicts(restore?: boolean): Promise<LanguageServerCommandResult>;
  toggleInWorkspace(enable: boolean): Promise<LanguageServerCommandResult>;
  installCommandLineTools(): Promise<LanguageServerCommandResult>;
  installKit(): Promise<LanguageServerCommandResult>;
  reviewChanges(clear?: boolean): Promise<LanguageServerCommandResult>;
}

/** Methods that map one-to-one onto a capability, so the bridge stays a table. */
const FORWARDED = {
  restartLanguageServer: "restart",
  restartEngine: "restartEngine",
  resetWorkspaceCache: "resetCache",
  selectContext: "selectContext",
  showModuleGraph: "moduleGraph",
  showLanguageServerLogs: "showLog",
  collectReport: "collectReport",
  exportDiagnosticBundle: "exportDiagnostics",
  runBuildToolInTerminal: "runBuildTool",
  installCommandLineTools: "installTools",
  installKit: "installKit",
} as const;

export function createLanguageServerBridge(environment: CapabilityEnvironment): LanguageServerBridge {
  const capabilities = new CapabilityRegistry(environment);
  let refreshInFlight: Promise<LanguageServerCommandResult> | undefined;

  const invoke = (key: string, ...args: unknown[]): Promise<LanguageServerCommandResult> =>
    capabilities.invoke(key, ...args);

  /**
   * Builds are frequent; a second refresh while one is in flight would restart
   * the language server twice for one edit. The single-flight promise is shared,
   * so both callers see the same outcome.
   */
  function refreshLanguageServerAfterBuild(): Promise<LanguageServerCommandResult> {
    if (refreshInFlight !== undefined) {
      return refreshInFlight;
    }
    refreshInFlight = invoke("reload").finally(() => {
      refreshInFlight = undefined;
    });
    return refreshInFlight;
  }

  const forwarded = Object.fromEntries(
    Object.entries(FORWARDED).map(([method, key]) => [method, () => invoke(key)]),
  ) as Record<keyof typeof FORWARDED, () => Promise<LanguageServerCommandResult>>;

  return {
    capabilities,
    invoke,
    isGone: (key) => capabilities.isGone(key),
    isUnconfirmed: (key) => capabilities.isUnconfirmed(key),
    dangerOf: (key) => capability(key)?.danger ?? "none",
    refreshLanguageServerAfterBuild,
    ...forwarded,
    // The two commands whose *argument* selects the direction.
    manageConflicts: (restore = false) => invoke(restore ? "restoreOtherCppFeatures" : "turnOffOtherCppFeatures"),
    toggleInWorkspace: (enable) => invoke(enable ? "turnOnInWorkspace" : "turnOffInWorkspace"),
    reviewChanges: (clear = false) => invoke(clear ? "reviewClear" : "reviewRun"),
  };
}
