/**
 * Which of mcppls's actions VS Code offers, and running them.
 *
 * Pure: the only VS Code-shaped things it needs arrive through `CapabilityEnvironment`, which is
 * exactly what the extension host supplies and what the tests fake.
 *
 * WHAT EXISTS IS mcppls's ANSWER. The backend asks mcppls (`mcppls status`, or the frozen table of
 * 0.0.18 and 0.0.19) and hands over its actions, each under the VS Code command mcppls names and the
 * arguments it is called with (design 2026-10-30 part 2 §5.3). An action mcppls names no VS Code
 * command for is not offered; before the backend has answered, an action is *greyed out*, not
 * hidden, so a slow answer cannot remove a feature.
 *
 * The first real use calls the command and classifies a failure: `command '…' not found` means the
 * action is gone for this session (an extension that has not registered it); anything else is a
 * failure of that one call and does not remove the action.
 */

import { CAPABILITIES, MCPPLS_EXTENSION_ID, capability } from "./contract";
import type { ServerAction } from "../ide/protocol";

const POLICY_KEYS = CAPABILITIES.map((entry) => entry.key);

export interface CapabilityEnvironment {
  /** `vscode.extensions.getExtension(id) !== undefined`. */
  extensionInstalled(id: string): boolean;
  /** mcppls's actions as the backend last reported them; `undefined` before it has answered. */
  serverActions(): readonly ServerAction[] | undefined;
  /** Activate the dependency before the first forward, when VS Code has not. */
  activateExtension?(id: string): Thenable<unknown>;
  executeCommand<T>(command: string, ...args: unknown[]): Thenable<T>;
}

export type CapabilityState = "available" | "declared" | "undeclared" | "missing" | "unavailable";

export interface CapabilityStatus {
  key: string;
  state: CapabilityState;
  /** The VS Code command mcppls names for the action, when it names one. */
  command?: string;
  /** What mcppls says the command is called with. */
  arguments?: readonly unknown[];
}

export type InvokeState = "completed" | "unavailable" | "missing" | "failed";

export interface InvokeResult {
  state: InvokeState;
  capabilityKey: string;
  /** The command that ran, when one ran. */
  command?: string;
  /** Error text from the failed call, verbatim. */
  error?: string;
  /**
   * Whatever the command answered with, when it answered with anything.
   *
   * A few mcppls commands return a path — `mcppls.exportDiagnosticBundle`
   * resolves to the zip it wrote (`exportDiagnosticBundle(): Promise<string |
   * undefined>` upstream) — and that is the only reliable way to point at the
   * file afterwards. Most commands return nothing, and the caller must treat this
   * as unknown: it is passed on untouched, never parsed.
   */
  value?: unknown;
}

/** `command 'x' not found` in the several spellings VS Code has used. */
const NOT_FOUND = /command\s+(?:['"`][^'"`]+['"`]\s+)?not found|not registered|no such command/i;

export function classifyCommandError(error: unknown): "missing" | "failed" {
  const message = error instanceof Error ? error.message : String(error);
  return NOT_FOUND.test(message) ? "missing" : "failed";
}

export class CapabilityRegistry {
  private readonly statuses = new Map<string, CapabilityStatus>();

  public constructor(private readonly environment: CapabilityEnvironment) {
    this.probe();
  }

  /** Re-read mcppls's actions; called when the backend answered again or an extension changed. */
  public invalidate(): void {
    this.statuses.clear();
    this.probe();
  }

  public status(key: string): CapabilityStatus {
    return this.statuses.get(key) ?? { key, state: "unavailable" };
  }

  /** Capabilities whose command is believed usable right now. */
  public availableKeys(): string[] {
    return [...this.statuses.values()]
      .filter((status) => status.state === "available" || status.state === "declared")
      .map((status) => status.key);
  }

  /** True when the user should not be offered the capability. */
  public isGone(key: string): boolean {
    const state = this.status(key).state;
    return state === "unavailable" || state === "missing";
  }

  /** True when mcppls has not answered yet: grey out, do not hide. */
  public isUnconfirmed(key: string): boolean {
    return this.status(key).state === "undeclared";
  }

  private probe(): void {
    const installed = this.environment.extensionInstalled(MCPPLS_EXTENSION_ID);
    const actions = installed ? this.environment.serverActions() : undefined;
    for (const action of actions ?? []) {
      if (capability(action.id) === undefined) continue;   // an action mcpp-ide has no policy for is not offered
      this.statuses.set(
        action.id,
        action.vscode === undefined
          ? { key: action.id, state: "unavailable" }
          : { key: action.id, state: "declared", command: action.vscode, ...(action.vscodeArguments ? { arguments: action.vscodeArguments } : {}) },
      );
    }
    if (actions !== undefined) return;
    // Not answered yet (or not installed): every action is unconfirmed, or unavailable without mcppls.
    for (const key of POLICY_KEYS) this.statuses.set(key, { key, state: installed ? "undeclared" : "unavailable" });
  }

  /**
   * Run a capability, activating the dependency first. Returns the command that
   * ran so the caller can name it in a log line.
   */
  public async invoke(key: string, ...args: unknown[]): Promise<InvokeResult> {
    if (capability(key) === undefined) {
      return { state: "failed", capabilityKey: key, error: `unknown capability ${key}` };
    }
    if (!this.environment.extensionInstalled(MCPPLS_EXTENSION_ID)) {
      return { state: "unavailable", capabilityKey: key };
    }
    const known = this.status(key);
    if (known.state === "missing" || known.state === "unavailable" || known.command === undefined) {
      // Not offered by this mcppls, remembered from an earlier call, or not answered yet: the
      // installed C++ Modules does not offer it now, and VS Code is not asked.
      return { state: "missing", capabilityKey: key };
    }
    const command = known.command;
    const callArgs = args.length > 0 ? args : [...(known.arguments ?? [])];
    try {
      await this.environment.activateExtension?.(MCPPLS_EXTENSION_ID);
      const value = await this.environment.executeCommand(command, ...callArgs);
      this.statuses.set(key, { ...known, state: "available" });
      return { state: "completed", capabilityKey: key, command, ...(value === undefined ? {} : { value }) };
    } catch (error) {
      if (classifyCommandError(error) === "missing") {
        this.statuses.set(key, { key, state: "missing" });
        return { state: "missing", capabilityKey: key, command };
      }
      return { state: "failed", capabilityKey: key, command, error: messageOf(error) };
    }
  }
}

function messageOf(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
