/**
 * The pages: the views of the activity bar (the project tree, the libraries, the cache), the panels
 * they open (a package's page, the settings, the graphs) and their commands, wired together. The
 * extension registers them at activation; mcpp-clion's workbench (webui/) registers the same
 * modules in the same way, so both IDEs show mcpp-vscode's pages.
 */
import * as vscode from "vscode";

import { registerCacheView } from "./cache/cacheView";
import { runMcpp } from "./cli/process";
import { LIBRARY_COMMANDS } from "./commands/ids";
import { onDidChange as onConfigurationChanged, read } from "./config/access";
import { registerSettingsPanel } from "./config/panel";
import { t } from "./i18n/t";
import { createLibraryDetailOpener } from "./library/detailPanel";
import { invalidateIndexCache, loadSnapshot } from "./library/indexLocator";
import { registerLibraryView } from "./library/libraryView";
import { watchProjectFiles } from "./library/projectWatcher";
import { MCPP_MANIFEST_GLOB } from "./projects/context";
import { findCurrentProject } from "./projects/current";
import type { McppProjectDiscovery } from "./projects/discovery";
import type { LanguageServerBlockSource } from "./views/languageServerView";
import { registerProjectView } from "./views/projectView";

export interface PagesDeps {
  output: vscode.OutputChannel;
  /** The mcpp the commands run (`mcpp.path` for the project's folder, else `mcpp`). */
  mcppExecutable: (project: McppProjectDiscovery | undefined) => string;
  /** The project view's C++ Modules block; absent where no mcppls bridge is (the workbench). */
  languageService?: LanguageServerBlockSource;
}

export function registerPages(context: vscode.ExtensionContext, deps: PagesDeps): void {
  const output = deps.output;
  // ── views ────────────────────────────────────────────────────────────────
  // Each view owns its own tree provider and its own commands; the project view
  // reads the manifest, the cache view runs mcpp's read-only queries, and the
  // C++ Modules view only forwards to mcppls.
  const applyViewVisibility = (): void => {
    // Literal keys, so the wiring gate can see them.
    // `mcpp.views.enabled` is the master switch: with it off every view is hidden.
    // Whether VS Code also drops the container's icon from the activity bar is
    // up to VS Code itself — an extension cannot mark its own container
    // hidden-when-empty (the setting's description says the same). The `when`
    // clauses in package.json are negated (`!mcpp.sidebarHidden`) so the default
    // state is visible — otherwise activation would never run to set the key back.
    void vscode.commands.executeCommand("setContext", "mcpp.sidebarHidden", !read<boolean>("mcpp.views.enabled"));
    void vscode.commands.executeCommand("setContext", "mcpp.views.project", read<boolean>("mcpp.views.project.show"));
    void vscode.commands.executeCommand("setContext", "mcpp.views.library", read<boolean>("mcpp.views.library.show"));
    void vscode.commands.executeCommand("setContext", "mcpp.views.cache", read<boolean>("mcpp.views.cache.show"));
  };
  applyViewVisibility();
  context.subscriptions.push(onConfigurationChanged(applyViewVisibility));

  // The language service is a block inside the project view, so its state source has
  // to exist before the view is registered.
  registerProjectView(context, { currentProject: findCurrentProject, languageService: deps.languageService });
  // The library ecosystem: a sidebar view plus an editor-area package page. The
  // detail page writes `mcpp.toml` through `mcpp add`, so it tells the view to
  // refresh — no save event fires for a file written outside the editor.
  // Keep the selected library project when an editor-area webview takes focus.
  let libraryProjectRoot: string | undefined = vscode.window.activeTextEditor?.document.uri.scheme === "file"
    || vscode.workspace.workspaceFolders?.length === 1 ? findCurrentProject()?.root : undefined;
  const libraryProject = (): string | undefined => libraryProjectRoot;
  const chooseLibraryProject = async (): Promise<string | undefined> => {
    const manifests = await vscode.workspace.findFiles(MCPP_MANIFEST_GLOB, "**/{node_modules,target,.git}/**", 200);
    const roots = [...new Set(manifests.map((uri) => uri.fsPath.slice(0, -"/mcpp.toml".length)))];
    const active = libraryProject();
    if (active && !roots.includes(active)) { roots.unshift(active); }
    const selected = await vscode.window.showQuickPick(roots.map((root) => ({ label: vscode.workspace.asRelativePath(root), description: root, root })), {
      title: t("Choose the target mcpp project"), placeHolder: t("Dependencies will be added to this project's mcpp.toml"),
    });
    if (selected) { libraryProjectRoot = selected.root; watchSelectedLibraryProject(); refreshLibraries(); }
    return selected?.root;
  };
  const openLibraryDetail = createLibraryDetailOpener(context, {
    mcppExecutable: (root) => deps.mcppExecutable(root ? { root, manifestPath: `${root}/mcpp.toml` } : undefined),
    projectRoot: libraryProject,
    chooseProject: chooseLibraryProject,
    onPackageOpened: (key) => library.select(key),
    output,
    isTrusted: () => vscode.workspace.isTrusted,
    onDependenciesChanged: () => { void library.refresh(); },
    onProjectFilesRead: (root, manifest, lock) => { if (root === libraryProjectRoot) { selectedLibraryPoll?.observe(manifest, lock); } },
  });
  const library = registerLibraryView(context, {
    projectRoot: libraryProject,
    mcppExecutable: () => deps.mcppExecutable(libraryProjectRoot ? { root: libraryProjectRoot, manifestPath: `${libraryProjectRoot}/mcpp.toml` } : undefined),
    openDetail: openLibraryDetail,
    output,
    isTrusted: () => vscode.workspace.isTrusted,
  });
  const dependencyWatcher = vscode.workspace.createFileSystemWatcher("**/mcpp.{toml,lock}");
  let selectedLibraryWatcher: vscode.Disposable | undefined;
  let selectedLibraryPoll: ReturnType<typeof watchProjectFiles> | undefined;
  let libraryTimer: NodeJS.Timeout | undefined;
  const refreshLibraries = (): void => {
    void selectedLibraryPoll?.check();
    if (libraryTimer) { clearTimeout(libraryTimer); }
    libraryTimer = setTimeout(() => {
      libraryTimer = undefined;
      void library.refresh();
      void openLibraryDetail.refresh();
    }, 120);
  };
  // A non-recursive RelativePattern also observes scratch/gitignored projects
  // which a workspace's recursive watcher may intentionally exclude.
  function watchSelectedLibraryProject(): void {
    selectedLibraryWatcher?.dispose();
    selectedLibraryWatcher = undefined;
    selectedLibraryPoll = undefined;
    if (!libraryProjectRoot) { return; }
    const watcher = vscode.workspace.createFileSystemWatcher(new vscode.RelativePattern(vscode.Uri.file(libraryProjectRoot), "mcpp.{toml,lock}"));
    selectedLibraryPoll = watchProjectFiles(libraryProjectRoot, refreshLibraries);
    selectedLibraryWatcher = vscode.Disposable.from(selectedLibraryPoll, watcher, watcher.onDidCreate(refreshLibraries), watcher.onDidChange(refreshLibraries), watcher.onDidDelete(refreshLibraries));
  }
  watchSelectedLibraryProject();
  context.subscriptions.push(
    { dispose: () => selectedLibraryWatcher?.dispose() },
    dependencyWatcher,
    dependencyWatcher.onDidCreate(refreshLibraries),
    dependencyWatcher.onDidChange(refreshLibraries),
    dependencyWatcher.onDidDelete(refreshLibraries),
    vscode.window.onDidChangeActiveTextEditor((editor) => { if (editor?.document.uri.scheme === "file") { const active = findCurrentProject()?.root; if (active && active !== libraryProjectRoot) { libraryProjectRoot = active; watchSelectedLibraryProject(); } refreshLibraries(); } }),
    vscode.workspace.onDidChangeWorkspaceFolders(() => { libraryProjectRoot = undefined; watchSelectedLibraryProject(); refreshLibraries(); }),
    vscode.workspace.onDidGrantWorkspaceTrust(refreshLibraries),
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (event.affectsConfiguration("mcpp.library") || event.affectsConfiguration("mcpp.path") || event.affectsConfiguration("mcpp.ui.language")) { refreshLibraries(); }
    }),
    { dispose: () => { if (libraryTimer) { clearTimeout(libraryTimer); } } },
  );
  // `registerLibraryView` registers its own webview provider; these are only the
  // entry points a menu or the project view can name.
  context.subscriptions.push(
    vscode.commands.registerCommand(LIBRARY_COMMANDS.search, async () => {
      await vscode.commands.executeCommand("mcpp.library.focus");
    }),
    vscode.commands.registerCommand(LIBRARY_COMMANDS.openDetail, async (id: unknown) => {
      if (typeof id === "string" && id.length > 0) {
        await openLibraryDetail(id);
      }
    }),
    vscode.commands.registerCommand(LIBRARY_COMMANDS.updateIndex, async () => {
      if (!vscode.workspace.isTrusted) {
        void vscode.window.showWarningMessage(t("This workspace is not trusted. mcpp commands may run external programs named by workspace settings; trust the workspace first."));
        return;
      }
      const root = libraryProject();
      const project = root ? { root, manifestPath: `${root}/mcpp.toml` } : undefined;
      if (read<string>("mcpp.library.indexPath").trim()) {
        output.appendLine(t("Updating the global mcpp index; a configured custom checkout is only reread."));
      }
      library.setUpdateStatus(t("Updating mcpp index…"));
      const result = await vscode.window.withProgress(
        { location: vscode.ProgressLocation.Notification, title: t("Refreshing the mcpp Package Index…") },
        () => runMcpp(vscode.workspace.isTrusted, deps.mcppExecutable(project), ["index", "update"], project?.root, { timeoutMs: 300_000 }),
      );
      if (result === undefined || result.exitCode !== 0) {
        library.setUpdateStatus(t("Index update failed; showing the last available local snapshot."));
        if (result?.stderr) { output.appendLine(result.stderr.trimEnd()); }
        void vscode.window.showErrorMessage(t("mcpp index update failed with exit code {0}", result?.exitCode ?? -1));
        return;
      }
      invalidateIndexCache();
      await library.refresh();
      await openLibraryDetail.refresh();
      const snapshot = await loadSnapshot({ projectRoot: root });
      library.setUpdateStatus(t("Index updated; {0} local packages.", snapshot.entries.length));
    }),
  );
  // There is no `mcpp.library.indexFound` context key and no `viewsWelcome`
  // entry for the library view. `paneview.ts`'s base `ViewPane` answers
  // `shouldShowWelcome() === false`; only the tree-shaped panes override it, so
  // welcome content can never render inside a `"type": "webview"` view. The
  // library view states its own empty case in the document instead, which is the
  // only place VS Code will show it.

  registerCacheView(context, {
    output,
    currentProject: findCurrentProject,
    mcppExecutable: (project) => deps.mcppExecutable(project),
    isTrusted: () => vscode.workspace.isTrusted,
  });
  registerSettingsPanel(context);
}
