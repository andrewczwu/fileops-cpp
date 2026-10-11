#!/usr/bin/env bash
# Print the CHANGELOG.md section for one version (Keep a Changelog format) as release notes.
# A pre-release without its own section falls back to the final version it leads to.
set -euo pipefail

VERSION="${1:?usage: changelog-section.sh X.Y.Z[-pre]}"

section() {
  awk -v version="$1" '
    /^## \[/ { in_section = index($0, "## [" version "]") == 1; if (in_section) { print; next } }
    in_section { print }
  ' CHANGELOG.md
}

notes="$(section "${VERSION}")"
if [[ -z "${notes}" && "${VERSION}" == *-* ]]; then
  notes="$(section "${VERSION%%-*}")"
fi
printf '%s\n' "${notes}"
