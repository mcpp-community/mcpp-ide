// WHETHER THIS EXTENSION AND A BACKEND WORK TOGETHER (mcpp design 2026-11-07, D15). A version is
// `X.Y.Z`: `X.Y` is the series, shared by the backend and every editor plugin, and `Z` each
// component's own patch. The handshake asks protocol 2 and this extension's series; a backend before
// 1.2 states no series, and its version's `X.Y` is taken.

export interface Handshake {
  protocol?: unknown;
  series?: unknown;
  serverInfo?: { version?: unknown };
}

export type Incompatibility =
  | { kind: "protocol"; protocol: unknown }
  | { kind: "series"; backendVersion: string; backendSeries: string; ownVersion: string; ownSeries: string };

/** `X.Y` of a version `X.Y` or `X.Y.Z`; undefined for anything else. */
export function seriesOf(version: unknown): string | undefined {
  if (typeof version !== "string") return undefined;
  const m = /^(\d+)\.(\d+)(\.\d+)?/.exec(version.trim());
  return m ? `${m[1]}.${m[2]}` : undefined;
}

/** Why the backend that answered `init` does not work with this extension at `ownVersion`, or undefined. */
export function incompatibility(init: Handshake, ownVersion: string): Incompatibility | undefined {
  if (init.protocol !== 2) return { kind: "protocol", protocol: init.protocol };
  const backendVersion = typeof init.serverInfo?.version === "string" ? init.serverInfo.version : "unknown";
  const backendSeries = (typeof init.series === "string" ? init.series : undefined) ?? seriesOf(backendVersion) ?? "unknown";
  const ownSeries = seriesOf(ownVersion) ?? "unknown";
  if (backendSeries === ownSeries && ownSeries !== "unknown") return undefined;
  return { kind: "series", backendVersion, backendSeries, ownVersion, ownSeries };
}
