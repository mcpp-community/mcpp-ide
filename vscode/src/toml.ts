// Editing help for mcpp.toml and build.mcpp, answered by the backend from mcpp's own key registry
// and build-program directives (toml/complete, toml/hover, toml/symbols, buildscript/complete).
// Text analysis only: no mcpp process runs, so it works in an untrusted workspace too. Whether the
// manifest is valid is mcpp's to say (diagnostics.ts).
import * as path from "node:path";
import * as vscode from "vscode";

import type { Session } from "./session";

interface Item {
  label: string;
  kind?: string;
  detail?: string;
  documentation?: string;
  insertText?: string;
  snippet?: boolean;
  range?: { start: { line: number; character: number }; end: { line: number; character: number } };
}

const KINDS: Record<string, vscode.CompletionItemKind> = {
  section: vscode.CompletionItemKind.Module,
  table: vscode.CompletionItemKind.Module,
  key: vscode.CompletionItemKind.Property,
  value: vscode.CompletionItemKind.Value,
  enum: vscode.CompletionItemKind.EnumMember,
  function: vscode.CompletionItemKind.Function,
  template: vscode.CompletionItemKind.Snippet,
  snippet: vscode.CompletionItemKind.Snippet,
};

function toItem(i: Item): vscode.CompletionItem {
  const item = new vscode.CompletionItem(i.label, KINDS[i.kind ?? "key"] ?? vscode.CompletionItemKind.Text);
  item.detail = i.detail;
  if (i.documentation) item.documentation = new vscode.MarkdownString(i.documentation);
  if (i.insertText) item.insertText = i.snippet ? new vscode.SnippetString(i.insertText) : i.insertText;
  if (i.range) item.range = new vscode.Range(i.range.start.line, i.range.start.character, i.range.end.line, i.range.end.character);
  return item;
}

export function registerToml(context: vscode.ExtensionContext, session: Session): void {
  const enabled = (key: string) => vscode.workspace.getConfiguration().get<boolean>(key) ?? true;
  const params = (doc: vscode.TextDocument, pos: vscode.Position) => ({ text: doc.getText(), line: pos.line, character: pos.character });
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider({ language: "mcpp-toml" }, {
      async provideCompletionItems(doc, pos) {
        if (!enabled("mcpp.toml.completion")) return [];
        const r = await session.request<{ items: Item[] }>("toml/complete", params(doc, pos)).catch(() => ({ items: [] as Item[] }));
        return r.items.map(toItem);
      },
    }, "[", ".", "=", "\"", " "),
    vscode.languages.registerHoverProvider({ language: "mcpp-toml" }, {
      async provideHover(doc, pos) {
        if (!enabled("mcpp.toml.hover")) return undefined;
        const r = await session.request<{ contents: string } | null>("toml/hover", params(doc, pos)).catch(() => null);
        return r ? new vscode.Hover(new vscode.MarkdownString(r.contents)) : undefined;
      },
    }),
    vscode.languages.registerDocumentSymbolProvider({ language: "mcpp-toml" }, {
      async provideDocumentSymbols(doc) {
        if (!enabled("mcpp.toml.navigation")) return [];
        type Sym = { name: string; kind: string; line: number; children?: Sym[] };
        const r = await session.request<Sym[]>("toml/symbols", { text: doc.getText() }).catch(() => [] as Sym[]);
        const convert = (s: Sym): vscode.DocumentSymbol => {
          const line = doc.lineAt(Math.min(s.line, doc.lineCount - 1)).range;
          const sym = new vscode.DocumentSymbol(s.name, "", s.kind === "table" ? vscode.SymbolKind.Namespace : vscode.SymbolKind.Property, line, line);
          sym.children = (s.children ?? []).map(convert);
          return sym;
        };
        return r.map(convert);
      },
    }),
    // build.mcpp is C++ (mcppls answers it); only `mcpp::` directives are completed here.
    vscode.languages.registerCompletionItemProvider({ language: "cpp", pattern: "**/build.mcpp" }, {
      async provideCompletionItems(doc, pos) {
        if (!enabled("mcpp.buildScript.intelligence") || path.basename(doc.fileName) !== "build.mcpp") return [];
        const r = await session.request<{ items: Item[] }>("buildscript/complete", params(doc, pos)).catch(() => ({ items: [] as Item[] }));
        return r.items.map(toItem);
      },
    }, ":"),
  );
}
