#!/usr/bin/env bash
# check-package.sh <package> <root inside the archive>
#
# What an editor package carries of the backend, checked on the system this runs on (CI runs it on
# every system): the ok-image bundle (mcpp design 2026-10-28 part 2 §15), exactly one
# backend/mcpp-ide.okb; an okrun for every system and architecture CI packs; and the one for this
# system starting the bundle.
set -euo pipefail
package="$1" prefix="$2"
dir="$(mktemp -d)"
trap 'rm -rf "$dir"' EXIT
unzip -q "$package" -d "$dir"
root="$dir/$prefix"

case "$(uname -s)" in Linux) os=linux ;; Darwin) os=macos ;; *) os=windows ;; esac
case "$(uname -m)" in x86_64|amd64|AMD64) arch=x86_64 ;; aarch64|arm64|ARM64) arch=aarch64 ;; *) arch="$(uname -m)" ;; esac
exe=""; [ "$os" = windows ] && exe=".exe"

answers() {   # <argv...>: the backend says who it is
    local said
    said="$("$@" --version)"
    echo "$said"
    grep -q '^mcpp-ide ' <<< "$said"
}

okbs="$(find "$root" -name '*.okb' | wc -l | tr -d ' ')"
[ "$okbs" = 1 ] || { echo "::error::$package carries $okbs ok-image bundles; one is expected"; exit 1; }
[ -f "$root/backend/mcpp-ide.okb" ] || { echo "::error::the bundle is not backend/mcpp-ide.okb"; exit 1; }
for want in linux-x86_64 linux-aarch64 macos-aarch64 windows-x86_64; do
    ls "$root/backend/okrun/$want/"okrun* > /dev/null 2>&1 || { echo "::error::$package carries no okrun for $want"; exit 1; }
done
[ -d "$root/bin" ] && { echo "::error::$package carries bin/ beside the bundle"; exit 1; }
okrun="$root/backend/okrun/$os-$arch/okrun$exe"
chmod +x "$okrun" 2>/dev/null || true
answers "$okrun" "$root/backend/mcpp-ide.okb"
echo "$package: one bundle; okrun for $os-$arch starts it"
