import { t } from "../i18n/t";

export type ProjectTaskKind = "build" | "run" | "test" | "clean" | "pack";
export type TaskState = "succeeded" | "failed" | "cancelled";

export interface ProjectTaskPlan {
  kind: ProjectTaskKind;
  title: string;
  args: string[];
}

export interface TaskCompletion {
  state: TaskState;
  exitCode?: number;
}

/**
 * The task's display name. Resolved per call, not at import time, so a change to
 * `mcpp.ui.language` applies without reloading the window.
 */
function taskTitle(kind: ProjectTaskKind): string {
  switch (kind) {
    case "build":
      return t("mcpp: Build");
    case "run":
      return t("mcpp: Run");
    case "test":
      return t("mcpp: Test");
    case "clean":
      return t("mcpp: Clean");
    case "pack":
      return t("mcpp: Pack");
  }
}

/** `mcpp.task.<kind>Args`, so a user can pass `-j 4` or `--quiet` without a wrapper. */
export const TASK_ARGUMENT_SETTINGS: Readonly<Record<Exclude<ProjectTaskKind, "pack">, string>> = {
  build: "mcpp.task.buildArgs",
  run: "mcpp.task.runArgs",
  test: "mcpp.task.testArgs",
  clean: "mcpp.task.cleanArgs",
};

/**
 * What the build, run and test tasks build for: `mcpp.build.target` (a triple, or a target set such
 * as `openkal`; empty is this machine) and `mcpp.build.profile` (`dev`, the default, or `release`).
 * Clean and pack take neither: pack chooses its own target and profile.
 */
export interface BuildSelection {
  target?: string;
  profile?: string;
}

export function selectionArgs(kind: ProjectTaskKind, selection: BuildSelection = {}): string[] {
  if (kind !== "build" && kind !== "run" && kind !== "test") {
    return [];
  }
  const out: string[] = [];
  const target = typeof selection.target === "string" ? selection.target.trim() : "";
  if (target !== "") {
    out.push("--target", target);
  }
  if (selection.profile === "release") {
    out.push("--release");
  }
  return out;
}

export function projectTaskPlan(
  kind: ProjectTaskKind,
  extraArgs: readonly string[] = [],
  selection: BuildSelection = {},
): ProjectTaskPlan {
  return {
    kind,
    title: taskTitle(kind),
    args: [kind, ...selectionArgs(kind, selection), ...extraArgs],
  };
}

export function shouldRefreshLanguageServerAfterTask(
  kind: ProjectTaskKind,
  completion?: TaskCompletion,
): boolean {
  return kind === "build" && completion !== undefined && completion.state !== "cancelled";
}

export function classifyTaskExit(exitCode: number | undefined): TaskCompletion {
  if (exitCode === undefined) {
    return { state: "cancelled" };
  }
  if (exitCode === 0) {
    return { state: "succeeded", exitCode };
  }
  return { state: "failed", exitCode };
}

export class McppOperationRegistry<T> {
  private readonly projectTokens = new Map<string, T>();

  private globalToken: T | undefined;

  beginProject(projectRoot: string, token: T): T | undefined {
    const active = this.projectTokens.get(projectRoot) ?? this.globalToken;
    if (active !== undefined) {
      return active;
    }
    this.projectTokens.set(projectRoot, token);
    return undefined;
  }

  finishProject(projectRoot: string, token: T): void {
    if (this.projectTokens.get(projectRoot) === token) {
      this.projectTokens.delete(projectRoot);
    }
  }

  beginGlobal(token: T): T | undefined {
    const activeProject = this.projectTokens.values().next().value as T | undefined;
    if (this.globalToken !== undefined) {
      return this.globalToken;
    }
    if (activeProject !== undefined) {
      return activeProject;
    }
    this.globalToken = token;
    return undefined;
  }

  finishGlobal(token: T): void {
    if (this.globalToken === token) {
      this.globalToken = undefined;
    }
  }

  public hasActive(): boolean {
    return this.projectTokens.size > 0 || this.globalToken !== undefined;
  }
}
