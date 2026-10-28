// Problems from mcpp, never from a regular expression over a terminal: each `job/diagnostic` of a
// build goes into the Problems panel located where mcpp says, a job's start clears its project's,
// and the manifest's own problems (`manifest/diagnostics`, `manifest/check`) sit on mcpp.toml.
import * as path from "node:path";
import * as vscode from "vscode";

import { severityOf, zeroBased } from "./mapping";
import type { Diagnostic } from "./protocol";
import type { Session } from "./session";

const SEVERITY: Record<string, vscode.DiagnosticSeverity> = {
  error: vscode.DiagnosticSeverity.Error,
  warning: vscode.DiagnosticSeverity.Warning,
  information: vscode.DiagnosticSeverity.Information,
  hint: vscode.DiagnosticSeverity.Hint,
};

export function toVscode(d: Diagnostic): vscode.Diagnostic {
  const start = zeroBased(d.range?.start);
  const end = zeroBased(d.range?.end);
  // A point range is widened to the rest of the line's word by VS Code itself when it is empty.
  const range = new vscode.Range(start.line, start.character, end.line, Math.max(end.character, start.character + 1));
  const out = new vscode.Diagnostic(range, d.message, SEVERITY[severityOf(d)]);
  out.source = d.source || "mcpp";
  if (d.code) out.code = d.code;
  return out;
}

export class Problems implements vscode.Disposable {
  private readonly build = vscode.languages.createDiagnosticCollection("mcpp");
  private readonly manifest = vscode.languages.createDiagnosticCollection("mcpp.toml");
  private readonly byRoot = new Map<string, Map<string, vscode.Diagnostic[]>>();
  private readonly disposables: vscode.Disposable[] = [this.build, this.manifest];

  constructor(session: Session) {
    this.disposables.push(
      session.on("job/started", (p: { root: string; kind: string }) => { if (p.kind === "build" || p.kind === "test") this.clear(p.root); }),
      session.on("job/diagnostic", (p: Diagnostic & { root: string }) => this.add(p.root, p)),
      session.on("job/finished", (p: { root?: string; kind?: string; diagnostics?: Diagnostic[] }) => {
        // The envelope's diagnostics that the stream did not already carry (a manifest error ends a
        // build before any event).
        if (!p.root) return;
        for (const d of p.diagnostics ?? []) if (d.path && !this.has(p.root, d)) this.add(p.root, d);
      }),
      session.on("manifest/diagnostics", (p: { root: string; diagnostics: Diagnostic[] }) => this.setManifest(p.root, p.diagnostics)),
      vscode.workspace.onDidSaveTextDocument(async (doc) => {
        if (path.basename(doc.fileName) !== "mcpp.toml") return;
        try {
          const r = await session.request<{ root: string; diagnostics: Diagnostic[] }>("manifest/check", { root: path.dirname(doc.fileName) });
          this.setManifest(r.root, r.diagnostics);
        } catch {
          // mcpp unavailable: the self-check says why.
        }
      }),
    );
  }

  private has(root: string, d: Diagnostic): boolean {
    const list = this.byRoot.get(root)?.get(d.path!) ?? [];
    return list.some((x) => x.message === d.message);
  }

  private add(root: string, d: Diagnostic): void {
    if (!d.path) return;
    if (!this.byRoot.has(root)) this.byRoot.set(root, new Map());
    const files = this.byRoot.get(root)!;
    const file = path.isAbsolute(d.path) ? d.path : path.join(root, d.path);
    const list = files.get(file) ?? [];
    list.push(toVscode(d));
    files.set(file, list);
    this.build.set(vscode.Uri.file(file), list);
  }

  private clear(root: string): void {
    for (const file of this.byRoot.get(root)?.keys() ?? []) this.build.delete(vscode.Uri.file(file));
    this.byRoot.delete(root);
  }

  private setManifest(root: string, diagnostics: Diagnostic[]): void {
    const file = vscode.Uri.file(path.join(root, "mcpp.toml"));
    const enabled = vscode.workspace.getConfiguration().get<boolean>("mcpp.toml.diagnostics.enabled") ?? true;
    this.manifest.set(file, enabled ? diagnostics.map(toVscode) : []);
  }

  dispose(): void {
    this.disposables.forEach((d) => d.dispose());
  }
}
