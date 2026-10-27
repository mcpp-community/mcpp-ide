#!/usr/bin/env bash
# check-package.sh <package> <root inside the archive>
#
# What an editor package carries of the backend, checked on the system this runs on (CI runs it on
# every system): with the ok-image bundle (mcpp design 2026-10-28 part 2 §15), exactly one
# backend/mcpp-ide.okb, an okrun for every system and architecture, and the one for this system
# starting the bundle; until the bundle is built, a program for every system, and the one for this
# system answering.
set -euo pipefail
package="$1" prefix="$2"
dir="$(mktemp -d)"
trap 'rm -rf "$dir"' EXIT
unzip -q "$package" -d "$dir"
root="$dir/$prefix"

case "$(uname -s)" in Linux) os=linux vs=linux ;; Darwin) os=macos vs=darwin ;; *) os=windows vs=win32 ;; esac
case "$(uname -m)" in x86_64|amd64|AMD64) arch=x86_64 va=x64 ;; aarch64|arm64|ARM64) arch=aarch64 va=arm64 ;; *) arch="$(uname -m)" va="$arch" ;; esac
exe=""; [ "$os" = windows ] && exe=".exe"

answers() {   # <argv...>: the backend says who it is
    local said
    said="$("$@" --version)"
    echo "$said"
    grep -q '^mcpp-ide ' <<< "$said"
}

okbs="$(find "$root" -name '*.okb' | wc -l | tr -d ' ')"
if [ "$okbs" != 0 ]; then
    [ "$okbs" = 1 ] || { echo "::error::$package carries $okbs ok-image bundles; one is expected"; exit 1; }
    [ -f "$root/backend/mcpp-ide.okb" ] || { echo "::error::the bundle is not backend/mcpp-ide.okb"; exit 1; }
    for want in linux-x86_64 linux-aarch64 macos-aarch64 windows-x86_64; do
        ls "$root/backend/okrun/$want/"okrun* > /dev/null 2>&1 || { echo "::error::$package carries no okrun for $want"; exit 1; }
    done
    okrun="$root/backend/okrun/$os-$arch/okrun$exe"
    chmod +x "$okrun" 2>/dev/null || true
    answers "$okrun" "$root/backend/mcpp-ide.okb"
    echo "$package: one bundle; okrun for $os-$arch starts it"
    exit 0
fi

[ -d "$root/backend" ] && { echo "::error::$package has backend/ but no bundle"; exit 1; }
echo "::notice::$package carries no ok-image bundle yet (mcpp's platforms and openkal 0.17.2); checking its per-system programs"
for want in linux-x64/mcpp-ide linux-arm64/mcpp-ide darwin-arm64/mcpp-ide win32-x64/mcpp-ide.exe; do
    [ -f "$root/bin/$want" ] || { echo "::error::$package carries no bin/$want"; exit 1; }
done
program="$root/bin/$vs-$va/mcpp-ide$exe"
chmod +x "$program" 2>/dev/null || true
answers "$program"
echo "$package: a program for every system; the one for $vs-$va answers"
