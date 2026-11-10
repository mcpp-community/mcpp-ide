#!/usr/bin/env bash
# release-plan.sh [TAG] -- the components a release tag publishes, and its version (design 2026-11-07, D15).
#
# A version is X.Y.Z: X.Y, the series, is shared by the backend and every editor plugin; Z is an editor
# plugin's own patch. The backend is always X.Y.0: the VS Code and CLion packages carry it, so a change
# of the backend changes every package, and it begins a series. Two kinds of tag:
#
#   vX.Y.0              a series release: every component, each at X.Y.0 (a change of the backend)
#   <plugin>-vX.Y.Z     one editor plugin's patch: vscode, clion or nvim, at X.Y.Z
#
# Prints `components=...`, `version=...` and `title=...` for $GITHUB_OUTPUT. Without a tag it only
# checks that every component states one series, the backend's two statements agree and the backend
# is its series' X.Y.0 (CI).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
declare -A version
version[backend]="$(sed -n 's/^version *= *"\(.*\)"/\1/p' "$root/mcpp.toml" | head -1)"
version[vscode]="$(sed -n 's/^  "version": "\(.*\)",$/\1/p' "$root/mide-plugins/vscode/package.json" | head -1)"
version[clion]="$(sed -n 's/^pluginVersion *= *\(.*\)$/\1/p' "$root/mide-plugins/clion/gradle.properties" | head -1)"
version[nvim]="$(sed -n "s/^M.version = '\(.*\)'$/\1/p" "$root/mide-plugins/nvim/lua/mcpp/session.lua" | head -1)"
backend_source="$(sed -n 's/^inline constexpr std::string_view kVersion = "\(.*\)";$/\1/p' "$root/src/server.cppm" | head -1)"
series_source="$(sed -n 's/^inline constexpr std::string_view kSeries = "\(.*\)";$/\1/p' "$root/src/server.cppm" | head -1)"
fail() { echo "release-plan: $*" >&2; exit 1; }
series() { [[ "$1" =~ ^([0-9]+)\.([0-9]+)\.[0-9]+$ ]] && echo "${BASH_REMATCH[1]}.${BASH_REMATCH[2]}"; }

for c in backend vscode clion nvim; do
    [ -n "${version[$c]}" ] && [ -n "$(series "${version[$c]}")" ] || fail "$c states no X.Y.Z version (${version[$c]:-none})"
done
[ "$backend_source" = "${version[backend]}" ] || fail "src/server.cppm says $backend_source and mcpp.toml ${version[backend]}"
the_series="$(series "${version[backend]}")"
[ "${version[backend]}" = "$the_series.0" ] \
    || fail "the backend is ${version[backend]}; it is always its series' X.Y.0 (the editor packages carry it, so a change of it begins a series)"
[ "$series_source" = "$the_series" ] || fail "src/server.cppm's kSeries is $series_source, the backend's series $the_series"
for c in vscode clion nvim; do
    [ "$(series "${version[$c]}")" = "$the_series" ] \
        || fail "$c is ${version[$c]}, of another series than the backend's ${version[backend]}"
done

tag="${1:-}"
if [ -z "$tag" ]; then
    echo "one series $the_series: backend ${version[backend]}, vscode ${version[vscode]}, clion ${version[clion]}, nvim ${version[nvim]}"
    exit 0
fi
if [[ "$tag" =~ ^v([0-9]+\.[0-9]+\.0)$ ]]; then
    v="${BASH_REMATCH[1]}"
    for c in backend vscode clion nvim; do
        [ "${version[$c]}" = "$v" ] || fail "the series release $tag needs every component at $v; $c is ${version[$c]}"
    done
    echo "components=backend vscode clion nvim"
    echo "version=$v"
    echo "title=mcpp-ide $v"
elif [[ "$tag" =~ ^backend-v ]]; then
    fail "$tag: the backend has no patch of its own; a change of it is a series release vX.Y.0"
elif [[ "$tag" =~ ^(vscode|clion|nvim)-v([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
    c="${BASH_REMATCH[1]}"; v="${BASH_REMATCH[2]}"
    [ "${version[$c]}" = "$v" ] || fail "the tag $tag names $c $v, and $c is ${version[$c]}"
    # A plugin's patch carries its series' backend: no path of the backend (.github/components.tsv)
    # has changed since the series' tag.
    if git -C "$root" rev-parse -q --verify "refs/tags/v$the_series.0" > /dev/null; then
        mapfile -t backend_rows < <(awk -F'\t' '$1 !~ /^(#|@)/ && NF == 2 {print $1 "\t" $2}' "$root/.github/components.tsv")
        while IFS= read -r path; do
            for row in "${backend_rows[@]}"; do
                pattern="${row#*$'\t'}"
                # shellcheck disable=SC2254
                case "$path" in $pattern)
                    [ "${row%%$'\t'*}" != backend ] \
                        || fail "$tag: the backend changed since v$the_series.0 ($path); a change of the backend is a series release"
                    break ;;
                esac
            done
        done < <(git -C "$root" diff --name-only "v$the_series.0" HEAD)
    fi
    declare -A names=([vscode]=mcpp-vscode [clion]=mcpp-clion [nvim]=mcpp-nvim)
    echo "components=$c"
    echo "version=$v"
    echo "title=${names[$c]} $v"
else
    fail "$tag is neither vX.Y.0 nor <vscode|clion|nvim>-vX.Y.Z"
fi
