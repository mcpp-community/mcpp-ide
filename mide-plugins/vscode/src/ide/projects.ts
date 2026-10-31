/**
 * The projects the backend-driven flows (debugging, testing, packing) act on, as the extension
 * already knows them: the current project is 0.9's discovery (`findNearestMcppProject` from the
 * active editor), the window's projects are the backend's answer at `initialize`.
 */
export interface IdeProject {
  root: string;
  name?: string;
}

export interface IdeProjects {
  /** The project of the active editor, else of the first workspace folder. */
  current(): IdeProject | undefined;
  /** Every project of the window's folders. */
  all(): IdeProject[];
}
