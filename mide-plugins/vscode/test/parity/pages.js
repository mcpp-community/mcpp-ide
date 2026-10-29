// The pages of mcpp-vscode, rendered from fixed view models by the generators of one compiled tree
// (`dist/` of this extension, or of mcpp-vscode 0.9.0). The parity gate renders both and expects the
// same documents: the same view model gives the same page, whatever the version.
//
// Plain JavaScript, not compiled with the tree it renders: it reaches the generators by path, so the
// same file drives 0.9.0's build and this one.
"use strict";

const path = require("node:path");

const ASSETS = {
  cspSource: "vscode-webview://parity",
  nonce: "parity-nonce",
  styleUri: "vscode-webview://parity/media/page.css",
  scriptUri: "vscode-webview://parity/media/page.js",
};

/** Every key of a generator's `*_UI` table, labelled with itself: the labels are not under test. */
function labels(...tables) {
  const ui = {};
  for (const table of tables) for (const key of Object.values(table)) if (typeof key === "string") ui[key] = key;
  return ui;
}

const bytes = (value) => `${value} B`;
const count = (value) => String(value);

function cachePages(dist) {
  const m = require(path.join(dist, "src/cache/cachePanelHtml.js"));
  const ui = labels(m.CACHE_PANEL_UI);
  const shared = {
    available: true,
    root: "/home/user/.mcpp/cache",
    totalBytes: 7340032,
    totalEntries: 42,
    byKind: [
      { kind: "package", entries: 30, bytes: 6291456 },
      { kind: "bmi", entries: 12, bytes: 1048576 },
    ],
    buckets: [
      { fromDays: 0, toDays: 7, entries: 20, bytes: 4194304 },
      { fromDays: 7, toDays: 30, entries: 15, bytes: 2097152 },
      { fromDays: 30, entries: 7, bytes: 1048576 },
    ],
    top: [
      { label: "compat.zlib@1.3.2", entries: 4, bytes: 2097152, oldestAccessed: 1760000000 },
      { label: "nlohmann.json@3.12.0", entries: 2, bytes: 1048576, oldestAccessed: 1761000000 },
    ],
    incomplete: 1,
    oldestAccessed: "2026-09-01T00:00:00.000Z",
    newestAccessed: "2026-10-29T00:00:00.000Z",
  };
  const base = {
    ui,
    project: { available: true, totalBytes: 3145728, files: 120, groups: 3, staleBytes: 1048576 },
    shared,
    legacy: { bytes: 524288, path: "/home/user/.mcpp/bmi" },
    format: { bytes, count },
    limits: { topN: 5, warnAboveGiB: 20, budgetGiB: 10 },
    estimate: "about 2 MiB would be dropped",
  };
  return [
    { name: "cache-full", html: m.renderCachePanelHtml(base, ASSETS) },
    {
      name: "cache-unavailable",
      html: m.renderCachePanelHtml({
        ...base,
        project: { available: false, note: "mcpp.toml was not found", totalBytes: 0, files: 0, groups: 0 },
        shared: { available: false, note: "mcpp cache list failed", totalBytes: 0, totalEntries: 0, byKind: [], buckets: [], top: [], incomplete: 0 },
        legacy: undefined,
        estimate: undefined,
      }, ASSETS),
    },
  ];
}

function libraryPages(dist) {
  const m = require(path.join(dist, "src/library/libraryHtml.js"));
  const ui = labels(m.LIBRARY_UI);
  const rows = [
    { id: "nlohmann.json", key: "mcpp-index:nlohmann.json", source: "mcpp-index", namespace: "nlohmann", name: "json",
      version: "3.12.0", surface: "header", surfaces: ["header", "module"], badges: ["examples"],
      haystack: "nlohmann json json for modern c++", description: "JSON for Modern C++", added: true, unreadable: false },
    { id: "compat.zlib", key: "mcpp-index:compat.zlib", source: "mcpp-index", namespace: "compat", name: "zlib",
      version: "1.3.2", surface: "header", surfaces: ["header"], badges: ["openkal-compat"],
      haystack: "compat zlib compression", description: "A compression library", added: false, unreadable: false },
    { id: "broken.pkg", key: "mcpp-index:broken.pkg", name: "pkg", surfaces: [], badges: [], haystack: "broken pkg",
      added: false, unreadable: true },
  ];
  const model = {
    ui, rows, selectedKey: "mcpp-index:nlohmann.json", query: "", networkSearch: false,
    networkSearchSetting: "mcpp.library.networkSearch", dataSource: "the local mcpp-index",
    sourceDetails: "/home/user/.mcpp/registry/data/mcpp-index", countTemplate: "{0} of {1}", total: 3,
  };
  return [
    { name: "library-list", html: m.renderLibraryHtml(model, ASSETS) },
    { name: "library-empty", html: m.renderLibraryHtml({ ...model, rows: [], total: 0, notice: "No library index was found." }, ASSETS) },
  ];
}

function detailPages(dist) {
  const m = require(path.join(dist, "src/library/detailHtml.js"));
  const ui = labels(m.DETAIL_UI);
  const model = {
    ui, key: "mcpp-index:nlohmann.json", generation: 1, projectRoot: "/work/app", platform: "linux",
    projectState: { declared: true, declaredVersion: "3.12.0", resolvedVersion: "3.12.0", dev: false, sourceType: "registry", ambiguous: false },
    manifestAvailable: true, id: "nlohmann.json", name: "json", description: "JSON for Modern C++",
    licenses: ["MIT"], repo: "https://github.com/nlohmann/json", registry: "mcpp-index",
    surface: "header", surfaces: ["header", "module"], badges: ["examples"],
    versions: [
      { platform: "linux", versions: ["3.12.0", "3.11.3"], current: true },
      { platform: "windows", versions: ["3.12.0"], current: false },
    ],
    currentVersions: ["3.12.0", "3.11.3"], latest: "3.12.0", installed: { version: "3.12.0", dev: false },
    standard: "c++23", dependencies: [{ id: "compat.zlib", version: "1.3.2", resolved: "1.3.2" }],
    includeDirs: ["include"], targets: ["json"],
    snippets: [{ file: "examples/hello.cpp", startLine: 1, lines: ["import nlohmann.json;", "int main() {}"], usageLine: 1 }],
    exampleProject: "examples", usage: ["import nlohmann.json;"], indexUrl: "https://mcpp-community.github.io/mcpp-index/nlohmann.json",
    commandTemplate: "mcpp add {0}@{1}", commandDevTemplate: "mcpp add --dev {0}@{1}", dataSource: "mcpp xpkg parse",
  };
  return [
    { name: "detail-installed", html: m.renderDetailHtml(model, ASSETS) },
    { name: "detail-loading", html: m.renderDetailHtml({ ...model, loading: true, installed: undefined, projectState: undefined }, ASSETS) },
  ];
}

function settingsPages(dist) {
  const m = require(path.join(dist, "src/config/panelHtml.js"));
  const ui = labels(m.PANEL_UI);
  const model = {
    ui,
    resourceLabel: "app",
    presets: [{ id: "quiet", title: "Quiet", description: "Fewer notifications." }],
    sections: [{
      id: "project", title: "Project", rows: [
        { key: "mcpp.path", title: "Set the mcpp executable path", description: "Path to the mcpp CLI.", type: "string",
          value: "", scope: "resource", applies: "immediate", source: "default", tier: "public", since: "0.1.0" },
        { key: "mcpp.task.revealTerminal", title: "Reveal the task terminal", description: "When to show the terminal.",
          type: "string", value: "onFailure", options: [{ value: "always", label: "always" }, { value: "onFailure", label: "onFailure" },
            { value: "never", label: "never" }], scope: "resource", applies: "next-build", source: "workspace", tier: "public", since: "0.5.0" },
        { key: "mcpp.cache.warnAboveGiB", title: "Warn above", description: "A size in GiB.", type: "number", value: 20,
          minimum: 1, maximum: 1024, scope: "window", applies: "immediate", source: "user", tier: "advanced", since: "0.6.0" },
        { key: "mcpp.task.buildArgs", title: "Build arguments", description: "Extra arguments.", type: "array", value: ["-j", "4"],
          scope: "resource", applies: "next-build", source: "default", tier: "public", since: "0.5.0" },
        { key: "mcpp.clangd.path", title: "clangd path", description: "Deprecated.", type: "string", value: "", scope: "resource",
          applies: "immediate", source: "default", tier: "advanced", deprecated: true, deprecationMessage: "No longer read.", since: "0.1.0" },
      ],
    }],
  };
  return [{ name: "settings", html: m.renderPanelHtml(model, ASSETS) }];
}

function graphPages(dist) {
  const m = require(path.join(dist, "src/graphs/panelHtml.js"));
  return [{ name: "graph", html: m.graphPanelHtml(ASSETS, "vscode-webview://parity/vendor/mermaid.js", "vscode-webview://parity/media/graph.js") }];
}

/** Every page of `dist`, rendered: `[{ name, html }]`. */
function pages(dist) {
  return [...cachePages(dist), ...libraryPages(dist), ...detailPages(dist), ...settingsPages(dist), ...graphPages(dist)];
}

module.exports = { pages, ASSETS };
