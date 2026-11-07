// D15: the extension works with a backend of its own series, whatever either patch is; it refuses
// another series, or another protocol, naming both versions.
import * as assert from "node:assert/strict";
import { test } from "node:test";

import { incompatibility, seriesOf } from "../../src/ide/compat";

test("one series works together, whatever the patches", () => {
  assert.equal(incompatibility({ protocol: 2, series: "1.2", serverInfo: { version: "1.2.0" } }, "1.2.3"), undefined);
  assert.equal(incompatibility({ protocol: 2, series: "1.2", serverInfo: { version: "1.2.4" } }, "1.2.0"), undefined);
  assert.equal(incompatibility({ protocol: 2, serverInfo: { version: "1.2.1" } }, "1.2.0"), undefined);
});

test("another series or protocol is refused, naming both versions", () => {
  assert.deepEqual(incompatibility({ protocol: 2, serverInfo: { version: "0.2.1" } }, "1.2.0"),
    { kind: "series", backendVersion: "0.2.1", backendSeries: "0.2", ownVersion: "1.2.0", ownSeries: "1.2" });
  assert.equal(incompatibility({ protocol: 2, series: "1.3", serverInfo: { version: "1.3.0" } }, "1.2.0")?.kind, "series");
  assert.equal(incompatibility({ protocol: 1, series: "1.2", serverInfo: { version: "1.2.0" } }, "1.2.0")?.kind, "protocol");
  assert.equal(incompatibility({}, "1.2.0")?.kind, "protocol");
  assert.equal(incompatibility({ protocol: 2, series: "1.2" }, "unknown")?.kind, "series");
});

test("the series of a version", () => {
  assert.equal(seriesOf("1.2.0"), "1.2");
  assert.equal(seriesOf("1.2"), "1.2");
  assert.equal(seriesOf("v1.2.0"), undefined);
  assert.equal(seriesOf(undefined), undefined);
});
