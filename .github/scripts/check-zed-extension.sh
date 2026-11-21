#!/usr/bin/env bash
# check-zed-extension.sh <extension directory> -- what Zed's registry checks of an extension before it
# builds one (zed-industries/extensions: src/lib/validation.js, src/lib/license.js, and the
# publishing prerequisites at zed.dev/docs/extensions/publishing/prerequisites), so that a pull request
# there does not fail on it.
set -euo pipefail
dir="${1:?the extension directory}"
manifest="$dir/extension.toml"
field() { sed -n "s/^$1 = \"\(.*\)\"$/\1/p" "$manifest" | head -1; }
fail() { echo "check-zed-extension: $*" >&2; exit 1; }
id="$(field id)"; name="$(field name)"; version="$(field version)"
[[ "$id" =~ ^[a-z0-9-]+$ ]] || fail "the id '$id' is not lowercase letters, digits and '-'"
case "$id" in zed-*|*-zed|*extension*) fail "the id '$id' starts with zed-, ends with -zed or names 'extension'" ;; esac
case "$name" in "Zed "*|*" Zed") fail "the name '$name' starts or ends with Zed" ;; esac
[[ "${name,,}" != *extension* ]] || fail "the name '$name' names 'extension'"
[[ "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || fail "the version '$version' is not X.Y.Z"
grep -qx 'schema_version = 1' "$manifest" || fail "schema_version is not 1"
# An extension that provides a language server and nothing else is named for it.
if ! grep -qE '^\[(grammars|languages)' "$manifest" && ! [ -d "$dir/languages" ] && ! [ -d "$dir/themes" ]; then
    case "$id" in *-language-server|*-lsp) ;; *) fail "the id '$id' provides a language server only and ends in neither -language-server nor -lsp" ;; esac
fi
# The license is read in the extension's own directory; the repository's root does not count.
license="$(find "$dir" -maxdepth 1 -type f -iname 'licen[cs]e*' | head -1)"
[ -n "$license" ] || fail "no LICENSE in $dir"
text="$(tr -s '[:space:]' ' ' < "$license")"
for pattern in 'Apache License' 'Version 2.0, January 2004' 'http://www.apache.org/licenses/' \
               'TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION' '1. Definitions.' \
               '2. Grant of Copyright License.' '3. Grant of Patent License.' '4. Redistribution.' \
               '5. Submission of Contributions.' '6. Trademarks.' '7. Disclaimer of Warranty.' \
               '8. Limitation of Liability.' '9. Accepting Warranty or Additional Liability.'; do
    grep -qiF "$pattern" <<<"$text" || fail "$license is not the Apache 2.0 license the registry accepts (no '$pattern')"
done
echo "check-zed-extension: $id $version ($name): the registry's checks of the manifest and the license pass"
