#!/usr/bin/env bash
# The classification of .github/scripts/changes.sh, on lists of paths.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
failures=0
check() {   # check <description> <event> <expected key=value...> -- <paths...>
    local what="$1" event="$2"; shift 2
    local want=()
    while [ "$1" != -- ]; do want+=("$1"); shift; done
    shift
    local list; list=$(mktemp)
    printf '%s\n' "$@" > "$list"
    local base=main
    [ "$event" = pull_request_release ] && { event=pull_request; base=release/1.2.0; }
    local got; got=$(EVENT="$event" BASE_REF="$base" HEAD_REF=topic bash "$here/changes.sh" "$list" 2>/dev/null)
    rm -f "$list"
    for kv in "${want[@]}"; do
        if ! grep -qx "$kv" <<<"$got"; then
            echo "FAIL: $what: wanted $kv, got: $(tr '\n' ' ' <<<"$got")"
            failures=$((failures + 1))
        fi
    done
}
check "a plugin alone" pull_request backend=false clion=true vscode=false nvim=false full=false -- mide-plugins/clion/src/main/kotlin/X.kt
check "its README is the plugin's" pull_request vscode=true docs=false backend=false -- mide-plugins/vscode/README.md
check "documentation alone" pull_request docs=true backend=false vscode=false -- docs/x.md README.md
check "the backend reaches every plugin" pull_request backend=true vscode=true clion=true nvim=true -- src/main.cpp
check "spec/ is the backend's" pull_request backend=true vscode=true -- spec/test-frameworks.json
check "a workflow reaches everything" pull_request full=true backend=true -- .github/workflows/ci.yml
check "a path of no component reaches everything" pull_request full=true -- newdir/x
check "a push reaches everything" push full=true backend=true -- docs/x.md
check "a release branch reaches everything" pull_request_release full=true -- docs/x.md
list=$(mktemp); echo docs/x.md > "$list"
d=$(EVENT=pull_request bash "$here/changes.sh" "$list" 2>/dev/null | sed -n 's/^digest=//p'); rm -f "$list"
[ "${#d}" -eq 24 ] || { echo "FAIL: no digest"; failures=$((failures + 1)); }
[ "$failures" -eq 0 ] && echo "test_changes: every case held"
exit "$failures"
