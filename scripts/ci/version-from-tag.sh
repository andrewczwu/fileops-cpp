#!/usr/bin/env bash
# Validate a release tag (vMAJOR.MINOR.PATCH[-prerelease]) and print `version=` and `prerelease=`
# lines for $GITHUB_OUTPUT. Refuses to release a version that has no section in CHANGELOG.md;
# a pre-release may use the section of the final version it leads to (1.2.0-rc.1 -> [1.2.0]).
set -euo pipefail

TAG="${1:?usage: version-from-tag.sh vX.Y.Z[-pre]}"

if [[ ! "${TAG}" =~ ^v([0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?)$ ]]; then
  echo "::error::tag '${TAG}' is not vMAJOR.MINOR.PATCH[-prerelease]" >&2
  exit 1
fi
VERSION="${BASH_REMATCH[1]}"
BASE="${VERSION%%-*}"
PRERELEASE=false
[[ "${VERSION}" != "${BASE}" ]] && PRERELEASE=true

has_section() { grep -qE "^## \[${1//./\\.}\]" CHANGELOG.md; }

if ! has_section "${VERSION}" && ! { [[ "${PRERELEASE}" == true ]] && has_section "${BASE}"; }; then
  echo "::error file=CHANGELOG.md::no '## [${VERSION}]' (or '## [${BASE}]') section in CHANGELOG.md; add release notes before tagging" >&2
  exit 1
fi

echo "version=${VERSION}"
echo "prerelease=${PRERELEASE}"
