/**
 * The C++ Modules language service we depend on, as **data**.
 *
 * Everything this extension knows about `sunrisepeak.mcpp-language-server`
 * (mcppls) lives here: its extension id, the actions we offer, and how badly
 * each one needs confirming. No `vscode` import, so the table is unit-testable
 * and `tools/` can read it.
 *
 * Two rules this file exists to keep:
 *
 * 1. **Our own commands never use the `mcppls.` prefix.** mcppls's S3 spec
 *    (S3-5.6-3) forbids a client from registering a command id the server
 *    advertises: `vscode-languageclient` registers those itself, and a collision
 *    stops the language client from starting. See `.agents/docs/mcppls-integration.md`.
 * 2. **Nothing here writes an `mcppls.*` setting.** The only settings we ever
 *    touch are our own `mcpp.*`. The two commands that change mcppls state
 *    (`turnOn/turnOffInWorkspace`) are mcppls's own, and the user triggers them.
 */

export const MCPPLS_EXTENSION_ID = "sunrisepeak.mcpp-language-server";

/** How much ceremony an action needs before it runs. */
export type Danger = "none" | "confirm" | "destructive";

/**
 * mcpp-ide's policy for one of mcppls's actions. The key is mcppls's own action id (`mcppls
 * status`'s `actions[].id`, mcppls S3-8-7); the command it runs in VS Code, and what it is called
 * with, are mcppls's answer, never a copy kept here (design 2026-10-30 part 2 §5.3). What stays here
 * is what mcpp-ide decides: the title it shows, how much confirmation the action needs, and what to
 * say when it is not there.
 */
export interface Capability {
  key: string;
  /** English; the UI resolves it through `src/i18n/t.ts`. */
  title: string;
  danger: Danger;
  /** Shown when the action is not offered; says what still works. */
  degradedHint: string;
  /** Extra sentence for a destructive action, spelling out what it does *not* touch. */
  confirmHint?: string;
}

export const CAPABILITIES: readonly Capability[] = [
  {
    key: "reload",
    title: "Refresh the C++ Modules build description",
    danger: "none",
    degradedHint:
      "The build finished, but the language service could not be refreshed. Run \"C++ Modules: Restart Language Server\" from the Command Palette.",
  },
  {
    key: "selectContext",
    title: "Select the C++ Modules analysis context",
    danger: "none",
    degradedHint: "Installed C++ Modules does not offer a context picker; analysis uses the default context.",
  },
  {
    key: "moduleGraph",
    title: "Show the module graph",
    danger: "none",
    degradedHint: "Installed C++ Modules does not offer a module graph.",
  },
  {
    key: "showLog",
    title: "Show the C++ Modules log",
    danger: "none",
    degradedHint: "Installed C++ Modules does not offer a log command; look for its output channel in the Output view.",
  },
  {
    key: "restart",
    title: "Restart the C++ Modules language server",
    danger: "none",
    degradedHint: "Installed C++ Modules does not offer a restart command.",
  },
  {
    key: "restartEngine",
    title: "Restart the C++ semantic engine",
    danger: "confirm",
    degradedHint: "Installed C++ Modules does not offer an engine restart.",
    confirmHint: "The semantic engine restarts; module preparation starts over and may take a while.",
  },
  {
    key: "resetCache",
    title: "Reset this workspace's C++ Modules cache",
    danger: "destructive",
    degradedHint: "Installed C++ Modules does not offer a cache reset.",
    confirmHint:
      "This discards the language server's model cache for this workspace and prepares it again, which can take minutes. mcpp's own build cache and the project's target/ directory are not touched.",
  },
  {
    key: "openLogDirectory",
    title: "Open the C++ Modules log folder",
    danger: "none",
    degradedHint:
      "Installed C++ Modules does not offer a reveal command; its log lives in the mcppls cache directory.",
  },
  {
    key: "collectReport",
    title: "Collect a C++ Modules diagnostic report",
    danger: "none",
    degradedHint: "Installed C++ Modules does not offer a report command.",
  },
  {
    key: "exportDiagnostics",
    title: "Export a C++ Modules diagnostic bundle",
    danger: "none",
    degradedHint: "Installed C++ Modules does not offer a diagnostic bundle.",
  },
  {
    key: "runBuildTool",
    title: "Run the build tool in a terminal",
    danger: "confirm",
    degradedHint: "Installed C++ Modules does not offer this action.",
    confirmHint: "The build tool runs in an integrated terminal, with its normal side effects.",
  },
  // `danger: "none"` because mcppls's own command asks where to act (this
  // workspace or user settings) and does nothing when that question is closed;
  // the settings it writes are the other extensions' own keys, never `mcppls.*`.
  {
    key: "turnOffOtherCppFeatures",
    title: "Turn off other C++ language services",
    danger: "none",
    degradedHint: "Installed C++ Modules does not offer this; turn the other extension's language features off in its own settings.",
  },
  {
    key: "restoreOtherCppFeatures",
    title: "Restore other C++ language services",
    danger: "none",
    degradedHint: "Installed C++ Modules does not offer this; turn the other extension's language features on in its own settings.",
  },
  {
    key: "turnOffInWorkspace",
    title: "Enable or disable C++ Modules in this workspace",
    danger: "confirm",
    degradedHint: "Installed C++ Modules does not offer a per-workspace switch.",
    confirmHint: "This writes mcppls.enable, a setting owned by the C++ Modules extension.",
  },
  {
    key: "turnOnInWorkspace",
    title: "Enable or disable C++ Modules in this workspace",
    danger: "confirm",
    degradedHint: "Installed C++ Modules does not offer a per-workspace switch.",
    confirmHint: "This writes mcppls.enable, a setting owned by the C++ Modules extension.",
  },
  {
    key: "installTools",
    title: "Install the C++ Modules command line tools",
    danger: "confirm",
    degradedHint: "Installed C++ Modules does not offer a command line tools installer.",
    confirmHint: "This may install system packages the language server needs.",
  },
  {
    key: "installKit",
    title: "Install the semantic kit",
    danger: "confirm",
    degradedHint: "This mcppls does not install kits; files are read with the openkal kit it carries.",
    confirmHint: "The C++ Modules server installs the target's semantic kit with xlings, in the background.",
  },
  {
    key: "reviewRun",
    title: "Review workspace changes",
    danger: "none",
    degradedHint: "Installed C++ Modules does not offer its review commands here.",
  },
  {
    key: "reviewClear",
    title: "Review workspace changes",
    danger: "none",
    degradedHint: "Installed C++ Modules does not offer its review commands here.",
  },
];

const BY_KEY = new Map(CAPABILITIES.map((capability) => [capability.key, capability]));

export function capability(key: string): Capability | undefined {
  return BY_KEY.get(key);
}

/**
 * The capability table's own invariants (must be empty).
 *
 * The "our ids never use the mcppls prefix" rule is checked where our ids live
 * (`src/commands/ids.ts`); this covers the table itself: a key is mcppls's action id, never a
 * command id of either extension.
 */
export function capabilityProblems(): string[] {
  const problems: string[] = [];
  const seen = new Set<string>();
  for (const entry of CAPABILITIES) {
    if (seen.has(entry.key)) {
      problems.push(`duplicate capability ${entry.key}`);
    }
    seen.add(entry.key);
    if (entry.key.includes(".")) {
      problems.push(`${entry.key} is a command id, not mcppls's action id`);
    }
    if (entry.danger !== "none" && entry.confirmHint === undefined) {
      problems.push(`${entry.key} needs confirmation but has no confirmHint`);
    }
  }
  return problems;
}
