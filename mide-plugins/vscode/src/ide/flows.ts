/**
 * The flows the backend answers and 0.9.0 did not have, in 0.9.0's form (quick picks and task
 * terminals): choosing what the build tasks build for, among the targets and the target sets the
 * installed mcpp knows; packing, with the formats, modes and linkages mcpp lists for the project.
 */
import * as vscode from "vscode";

import { read } from "../config/access";
import { t } from "../i18n/t";
import type { IdeProjects } from "./projects";
import type { TargetRow } from "./protocol";
import type { Session } from "./session";

interface TargetPick extends vscode.QuickPickItem {
  row: TargetRow & { set?: boolean; members?: string[]; label?: string };
}

/** `mcpp.selectTarget`: writes `mcpp.build.target` for the workspace; this machine clears it. */
export async function selectTarget(session: Session, projects: IdeProjects, readOnly = false): Promise<void> {
  const root = projects.current()?.root;
  const r = await vscode.window.withProgress(
    { location: vscode.ProgressLocation.Window, title: t("mcpp: reading the targets…") },
    () => session.request<{ host: string; rows: TargetPick["row"][] }>("targets/list", { root }),
  );
  const picks: TargetPick[] = r.rows.map((row) => ({
    label: `${row.selected ? "$(check) " : ""}${row.set ? row.label ?? row.target : row.target}`,
    description: [row.host ? t("this machine") : "", row.set ? t("every architecture of the platform") : "", row.platformText]
      .filter(Boolean).join(" · "),
    detail: row.set ? undefined : row.toolchains.map((tc) => `${tc.spec || "—"} (${tc.status})`).join(", ") || undefined,
    row,
  }));
  const pick = await vscode.window.showQuickPick(picks, {
    title: readOnly ? t("The targets mcpp builds for") : t("Select the build target"),
    placeHolder: readOnly ? undefined : t("build, run, test and debug use it"),
  });
  if (!pick || readOnly) return;
  await vscode.workspace.getConfiguration().update("mcpp.build.target", pick.row.host ? "" : pick.row.target,
    vscode.ConfigurationTarget.Workspace);
  if (!pick.row.set && !pick.row.installed && pick.row.pin) {
    const install = t("Install");
    if ((await vscode.window.showInformationMessage(t("No toolchain is installed for {0}.", pick.row.target), install)) === install) {
      await vscode.commands.executeCommand("mcpp.installToolchain", pick.row.pin);
    }
  }
}

interface PackFormats {
  formats?: { name: string; builtin?: boolean; providers?: string[] }[];
  modes?: string[];
  linkages?: string[];
}

/**
 * `mcpp.pack`: three quick picks (format, mode, linkage) over what mcpp lists for the project, the
 * pack's target taken from `mcpp.build.target`; the arguments for the pack task, or undefined when
 * the user left a pick.
 */
export async function packArguments(session: Session, root: string): Promise<string[] | undefined> {
  const r = await vscode.window.withProgress(
    { location: vscode.ProgressLocation.Window, title: t("mcpp: reading the package formats…") },
    () => session.request<PackFormats>("pack/formats", { root }),
  );
  const projectDefault = t("the project's default");
  const step = async (title: string, values: readonly { label: string; description?: string; value: string }[]) =>
    vscode.window.showQuickPick([{ label: projectDefault, value: "" }, ...values], { title });
  const format = await step(t("mcpp: Pack (1/3): the package format"),
    (r.formats ?? []).map((f) => ({ label: f.name, description: f.builtin ? undefined : (f.providers ?? []).join(", "), value: f.name })));
  if (!format) return undefined;
  const mode = await step(t("mcpp: Pack (2/3): what the machine that runs it provides"),
    (r.modes ?? []).map((m) => ({ label: m, value: m })));
  if (!mode) return undefined;
  let linkage: { value: string } | undefined = { value: "" };
  if ((r.linkages ?? []).length > 0) {
    linkage = await step(t("mcpp: Pack (3/3): the libraries inside the program or beside it"),
      (r.linkages ?? []).map((l) => ({ label: l, value: l })));
    if (!linkage) return undefined;
  }
  const args: string[] = [];
  if (format.value) args.push("--pkg-format", format.value);
  if (mode.value) args.push("--mode", mode.value);
  if (linkage.value) args.push("--linkage", linkage.value);
  const target = (read<string>("mcpp.build.target", vscode.Uri.file(root)) ?? "").trim();
  if (target) args.push("--target", target);
  return args;
}
