#!/usr/bin/env bash
# Captures the golden fixtures under protocol/fixtures from a real mcpp: the
# envelopes, streams and records the backend reads. Both adapters' tests and
# the backend's mock mcpp (src/bin/mockmcpp.cpp) replay them, so a change in
# mcpp's contract shows up as a diff here first.
#
# Usage: MCPP=/path/to/mcpp tools/capture-fixtures.sh
set -euo pipefail
MCPP="${MCPP:-mcpp}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/protocol/fixtures"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$OUT"
export NO_COLOR=1 MCPP_CLIENT=mcpp-ide-fixtures

# Paths in the fixtures are the temporary project's; the replay rewrites
# @ROOT@ to the directory a test runs in.
scrub() { sed -e "s#$TMP/app#@ROOT@#g" -e "s#$HOME#@HOME@#g"; }

"$MCPP" --version | head -1 > "$OUT/version.txt"
"$MCPP" --protocol-version > "$OUT/protocol-version.json"

cd "$TMP"
"$MCPP" new app > /dev/null
cd app
"$MCPP" build --message-format json 2> /dev/null | scrub > "$OUT/build-ok.ndjson"
python3 - "$TMP/app/target/.last-result.json" > "$OUT/result-ok.json" <<'PY'
import json, sys
last = json.load(open(sys.argv[1]))
print(json.dumps(json.load(open(last["data"]["result"])), indent=2))
PY
sed -i "s#$TMP/app#@ROOT@#g; s#$HOME#@HOME@#g" "$OUT/result-ok.json"
"$MCPP" run --print-launch 2> /dev/null | scrub > "$OUT/launch.json"
"$MCPP" test --message-format json 2> /dev/null | scrub > "$OUT/test.ndjson" || true
"$MCPP" emit build-database --format json 2> /dev/null | scrub > "$OUT/build-database.json"
"$MCPP" search --format json 2> /dev/null | head -c 2000000 | scrub > "$OUT/search.json" || true
"$MCPP" index list --format json 2> /dev/null | scrub > "$OUT/index-list.json"
"$MCPP" pack --list-formats 2> /dev/null | scrub > "$OUT/pack-formats.json"
"$MCPP" toolchain list --format json 2> /dev/null | scrub > "$OUT/toolchain-list.json"
"$MCPP" cache list --format json 2> /dev/null | scrub > "$OUT/cache-list.json"
"$MCPP" self env --format json 2> /dev/null | scrub > "$OUT/self-env.json"
printf 'int main() { return undeclared_name; }\n' > src/main.cpp
"$MCPP" build --message-format json 2> /dev/null | scrub > "$OUT/build-failed.ndjson" || true
printf '[package\nname = "app"\n' > mcpp.toml
"$MCPP" build --message-format json 2> /dev/null | scrub > "$OUT/build-manifest-error.ndjson" || true
# A developer's home holds hundreds of cache entries and the index hundreds of
# packages; the fixtures keep enough of each to exercise every field.
python3 - "$OUT" <<'PY'
import json, sys, os
out = sys.argv[1]
for name, key, n in (("cache-list.json", "entries", 40), ("search.json", "packages", 60)):
    p = os.path.join(out, name)
    d = json.load(open(p))
    d["data"][key] = d["data"][key][:n]
    json.dump(d, open(p, "w"), indent=2)
PY
echo "fixtures from $(cat "$OUT/version.txt") in $OUT"
