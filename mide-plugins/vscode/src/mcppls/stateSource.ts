/**
 * The `vscode`-facing half of the C++ Modules state read.
 *
 * `state.ts` normalises what it is given; this file adds what VS Code knows (whether the extension
 * is installed and enabled). The state itself is `mcppls status` as the backend reported it
 * (`Session.languageService`, design 2026-10-30 part 2 §5.2); the mcppls extension's API object is
 * not read.
 *
 * `mcpp.languageService.readState` can switch the state off: the view then shows the degraded
 * content it always has for an installed-but-unreadable mcppls.
 */

import * as vscode from "vscode";

import { read } from "../config/access";
import { MCPPLS_EXTENSION_ID } from "./contract";
import type { LanguageServiceStatus } from "../ide/protocol";
import { localOtherCppFeatures, type OtherCppFeatures } from "./otherCppFeatures";
import { readStateFromStatus, type McpplsStateView } from "./state";

/** Why a state read did not happen. The view prints it verbatim. */
export function stateUnavailableReason(reason: "disabled" | "missing"): string {
  return reason === "disabled"
    ? "reading the C++ Modules state is turned off (mcpp.languageService.readState)"
    : `mcppls ${MCPPLS_EXTENSION_ID} is not installed`;
}

function dependency(): vscode.Extension<unknown> | undefined {
  return vscode.extensions.getExtension(MCPPLS_EXTENSION_ID);
}

export function languageServerInstalled(): boolean {
  return dependency() !== undefined;
}

export function languageServerVersion(): string | undefined {
  const version = (dependency()?.packageJSON as { version?: string } | undefined)?.version;
  return typeof version === "string" ? version : undefined;
}

/** `mcppls.enable`, read-only: the setting belongs to the other extension. */
export function languageServerEnabled(): boolean {
  return vscode.workspace.getConfiguration("mcppls").get<boolean>("enable", true) !== false;
}

/**
 * Whether C++ Modules serves this workspace's C++ files, `build.mcpp` included:
 * installed and enabled in VS Code, and not switched off here (`mcppls.enable`).
 * Read-only, like everything else in this file.
 */
export function languageServerServes(): boolean {
  return languageServerInstalled() && languageServerEnabled();
}

export function readLanguageServerState(status: LanguageServiceStatus | undefined): McpplsStateView {
  const meta = { version: languageServerVersion(), enabled: languageServerEnabled() };
  // The gate: `read()` falls back to the declared default (true) when the registry has no value.
  if (read<boolean>("mcpp.languageService.readState") === false) {
    return { available: false, reason: stateUnavailableReason("disabled"), ...meta };
  }
  if (dependency() === undefined) {
    return { available: false, reason: stateUnavailableReason("missing"), ...meta };
  }
  return readStateFromStatus(status, meta);
}

/**
 * The other C++ language services serving next to C++ Modules: a read-only look at the candidates'
 * installation and their own settings.
 */
export function readOtherCppFeatures(): OtherCppFeatures {
  return localOtherCppFeatures(
    (extensionId) => vscode.extensions.getExtension(extensionId) !== undefined,
    (section, key) => vscode.workspace.getConfiguration(section).get(key),
  );
}
