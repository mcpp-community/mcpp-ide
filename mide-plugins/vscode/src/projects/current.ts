/**
 * Which mcpp project the user is in: the nearest one above the active editor's file, within its
 * workspace folder unless `mcpp.project.discoveryBoundary` says otherwise, else the first one a
 * workspace folder holds. The extension and the workbench mcpp-clion runs the pages in ask the same.
 */
import * as vscode from "vscode";

import { read } from "../config/access";
import { discoveryBoundaryOf, findNearestMcppProject, type DiscoveryBoundary, type McppProjectDiscovery } from "./discovery";

export function discoveryBoundaryFromSettings(resource?: vscode.Uri): DiscoveryBoundary {
  return discoveryBoundaryOf(read<string>("mcpp.project.discoveryBoundary", resource));
}

export function findCurrentProject(): McppProjectDiscovery | undefined {
  const activeEditor = vscode.window.activeTextEditor;
  if (activeEditor !== undefined) {
    const activeUri = activeEditor.document.uri;
    if (activeUri.scheme !== "file") {
      return undefined;
    }
    const workspaceFolder = vscode.workspace.getWorkspaceFolder(activeUri);
    if (workspaceFolder === undefined) {
      return undefined;
    }
    return findNearestMcppProject(
      activeUri.fsPath,
      workspaceFolder.uri.fsPath,
      discoveryBoundaryFromSettings(workspaceFolder.uri),
    );
  }

  for (const workspaceFolder of vscode.workspace.workspaceFolders ?? []) {
    const project = findNearestMcppProject(
      workspaceFolder.uri.fsPath,
      workspaceFolder.uri.fsPath,
      discoveryBoundaryFromSettings(workspaceFolder.uri),
    );
    if (project !== undefined) {
      return project;
    }
  }
  return undefined;
}
