/**
 * Which other C++ language services are serving next to C++ Modules, normalised.
 *
 * Pure, like `./state.ts`: no `vscode`, nothing here throws. The read is local and read-only:
 * mcppls's three candidates, from the installed extensions and their settings. The mcppls
 * extension's API object is not read (design 2026-10-30 part 2 §5.2); whether there is something to
 * restore is mcppls's own command's answer, so `canRestore` says only whether mcppls offers the
 * command, and the caller sets it.
 *
 * This extension only *reads*. Turning a service off or restoring it
 * is mcppls's command (`mcppls.turnOffOtherCppFeatures`, which asks where), and
 * the settings it writes are the other extensions' own, never `mcppls.*`.
 */

export interface OtherCppService {
  extensionId: string;
  displayName: string;
  /** `true` when a setting turns its language features off; `false` (ccls) means disabling the extension. */
  settable: boolean;
}

export interface OtherCppFeatures {
  active: OtherCppService[];
  /** mcppls offers to put back the settings it changed. */
  canRestore: boolean;
}

/** A candidate the local read knows; `section`/`key`/`off` is how its language features are turned off. */
interface Candidate {
  extensionId: string;
  displayName: string;
  setting?: { section: string; key: string; off: unknown };
}

/** The same three mcppls checks (its `conflictCandidates.ts`), for when it cannot be asked. */
export const OTHER_CPP_CANDIDATES: readonly Candidate[] = [
  { extensionId: "ms-vscode.cpptools", displayName: "C/C++", setting: { section: "C_Cpp", key: "intelliSenseEngine", off: "disabled" } },
  { extensionId: "llvm-vs-code-extensions.vscode-clangd", displayName: "clangd", setting: { section: "clangd", key: "enable", off: false } },
  { extensionId: "ccls-project.ccls", displayName: "ccls" },
];

/** The settings whose change can make a candidate active or inactive. */
export const OTHER_CPP_SETTINGS: readonly string[] = OTHER_CPP_CANDIDATES.flatMap((candidate) =>
  candidate.setting === undefined ? [] : [`${candidate.setting.section}.${candidate.setting.key}`],
);

/**
 * The local read. `installed` answers for installed *and enabled* extensions
 * (`vscode.extensions.getExtension`); `value` is the effective setting.
 */
export function localOtherCppFeatures(
  installed: (extensionId: string) => boolean,
  value: (section: string, key: string) => unknown,
): OtherCppFeatures {
  const active = OTHER_CPP_CANDIDATES.filter(
    (candidate) =>
      installed(candidate.extensionId) &&
      (candidate.setting === undefined || value(candidate.setting.section, candidate.setting.key) !== candidate.setting.off),
  ).map((candidate) => ({
    extensionId: candidate.extensionId,
    displayName: candidate.displayName,
    settable: candidate.setting !== undefined,
  }));
  return { active, canRestore: false };
}
