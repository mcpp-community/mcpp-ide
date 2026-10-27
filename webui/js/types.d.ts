// The panels' one global, for `tsc --checkJs` (vscode: npm run check:webui). Not loaded at run time.
interface McppPanel {
  title(params: any): string;
  mount(el: HTMLElement, params: any): void | (() => void);
}

interface McppUiApi {
  panels: Record<string, McppPanel>;
  setBundle(b: Record<string, string>): void;
  t(key: string, ...args: unknown[]): string;
  formatBytes(bytes: number): string;
  ago(when: number | null | undefined, now?: number): string;
  debounce(fn: (...args: any[]) => void, ms: number): (...args: any[]) => void;
  h(spec: string, props?: any, ...children: any[]): any;
  clear<E extends HTMLElement>(el: E): E;
  badge(state: string, text: string): HTMLElement;
  button(text: string, onclick: (ev?: Event) => unknown, opts?: { secondary?: boolean; disabled?: boolean; title?: string }): HTMLButtonElement;
  section(title: string, ...children: any[]): HTMLElement;
  empty(text: string): HTMLElement;
  errorBox(error: any): HTMLElement;
  rpc(method: string, params?: unknown): Promise<any>;
  host(action: string, params?: unknown): Promise<any>;
  on(method: string, fn: (...args: any[]) => void): () => void;
  ready(): void;
  state: { get(): any; set(s: any): void };
  editor: "vscode" | "jetbrains";
  graphLayout: {
    layout(nodes: any[], edges: any[], opts?: any): { nodes: any[]; edges: any[]; width: number; height: number };
    neighbourhood(nodes: any[], edges: any[], focus: string, depth?: number): { nodes: any[]; edges: any[] };
  };
  cleanup(action: string, root?: string): Promise<boolean>;
}

interface Window {
  McppUi: McppUiApi;
  acquireVsCodeApi?: () => { postMessage(m: unknown): void; getState(): any; setState(s: any): void };
  mcppHostSend?: (text: string) => void;
  mcppHostReceive: (m: unknown) => void;
  mcppHostAttached: () => void;
  __mcppState: any;
}

declare var McppUi: McppUiApi;
