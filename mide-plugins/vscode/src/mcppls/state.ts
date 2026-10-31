/**
 * What the C++ Modules language service reports about the workspace, normalised.
 *
 * Pure: it takes the backend's `languageService/status` answer, whose `workspace` is what
 * `mcppls status` printed (mcppls S3 section 8, mcppls 0.0.20 and later), and answers with a shape
 * the project view renders, or with `available: false` and why. The three IDEs show the same
 * document (design 2026-10-30 part 2 §5.2); nothing here reads the mcppls extension's API object.
 * Nothing here throws.
 */

import type { LanguageServiceStatus } from "../ide/protocol";

export interface McpplsIssue {
  code: string;
  message: string;
}

export const MCPPLS_STATES = ["ready", "starting", "degraded", "stopped", "none"] as const;
export type McpplsState = (typeof MCPPLS_STATES)[number];

export interface McpplsStateView {
  available: boolean;
  /** Why it is unavailable, for the log and the tooltip; never shown as an error. */
  reason?: string;
  version?: string;
  enabled?: boolean;
  /** The best state among the servers serving the workspace; `none` when none ever did. */
  state?: McpplsState;
  engine?: { name: string; version: string; restarts?: number };
  kit?: { name: string; state: string; install?: string; inUse?: string };
  mcpp?: { version: string; program?: string };
  /** How many servers (editors) serve the workspace now. */
  instances?: number;
  issues?: McpplsIssue[];
}

export interface McpplsMeta {
  version?: string;
  enabled?: boolean;
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function text(value: unknown): string | undefined {
  return typeof value === "string" && value.length > 0 ? value : undefined;
}

function knownState(value: unknown): McpplsState | undefined {
  return typeof value === "string" && (MCPPLS_STATES as readonly string[]).includes(value) ? (value as McpplsState) : undefined;
}

/** The view of the backend's answer, `undefined` before the backend has answered. */
export function readStateFromStatus(status: LanguageServiceStatus | undefined, meta: McpplsMeta = {}): McpplsStateView {
  const base: McpplsStateView = { available: false, version: status?.version || meta.version, enabled: meta.enabled };
  if (status === undefined) {
    return { ...base, reason: "the mcpp-ide backend has not answered yet" };
  }
  if (status.state !== "ok") {
    return { ...base, reason: status.message };
  }
  if (status.statusSource !== "status" || !isRecord(status.workspace)) {
    return {
      ...base,
      reason:
        status.statusError !== undefined
          ? `mcppls status: ${status.statusError}`
          : `mcppls ${status.version} has no status command; 0.0.20 and later state the workspace's state`,
    };
  }
  const workspace = status.workspace as Record<string, unknown>;
  const state = knownState(workspace.state);
  if (state === undefined) {
    return { ...base, reason: `unknown state ${JSON.stringify(workspace.state)}` };
  }
  const view: McpplsStateView = { ...base, available: true, state, instances: Array.isArray(status.instances) ? status.instances.length : 0 };
  if (isRecord(workspace.engine) && text(workspace.engine.name) !== undefined) {
    const restarts = workspace.engine.restarts;
    view.engine = {
      name: text(workspace.engine.name) as string,
      version: text(workspace.engine.version) ?? "",
      ...(typeof restarts === "number" && Number.isFinite(restarts) ? { restarts } : {}),
    };
  }
  if (isRecord(workspace.kit) && text(workspace.kit.state) !== undefined) {
    view.kit = {
      name: text(workspace.kit.name) ?? "",
      state: text(workspace.kit.state) as string,
      ...(text(workspace.kit.install) !== undefined ? { install: text(workspace.kit.install) } : {}),
      ...(text(workspace.kit.inUse) !== undefined ? { inUse: text(workspace.kit.inUse) } : {}),
    };
  }
  if (isRecord(workspace.mcpp) && text(workspace.mcpp.version) !== undefined) {
    view.mcpp = { version: text(workspace.mcpp.version) as string, program: text(workspace.mcpp.program) };
  }
  if (Array.isArray(workspace.problems)) {
    const issues = workspace.problems
      .map((entry): McpplsIssue | undefined =>
        isRecord(entry) && text(entry.code) !== undefined && text(entry.message) !== undefined
          ? { code: text(entry.code) as string, message: text(entry.message) as string }
          : undefined,
      )
      .filter((entry): entry is McpplsIssue => entry !== undefined);
    if (issues.length > 0) view.issues = issues;
  }
  return view;
}

/** One line for the environment self-check; never throws. */
export function describeState(view: McpplsStateView): string {
  if (!view.available) {
    return `unavailable (${view.reason ?? "unknown reason"})`;
  }
  const parts: string[] = [view.state ?? "unknown"];
  if (view.engine !== undefined) parts.push(`engine ${view.engine.name} ${view.engine.version}`.trimEnd());
  if (view.kit !== undefined) parts.push(`kit ${view.kit.name} (${view.kit.state})`);
  if (view.instances !== undefined && view.instances > 1) parts.push(`${view.instances} servers`);
  if (view.issues !== undefined) parts.push(`${view.issues.length} issue(s)`);
  return parts.join(" · ");
}
