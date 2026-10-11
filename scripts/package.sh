#!/usr/bin/env bash
# Build the Debian package with CPack from the gcc-release preset and verify what was produced.
# Inputs (env): VERSION (default 0.0.0-dev), SOURCE_DATE_EPOCH (optional)
# Output: artifacts/deb/fileops-cpp_<version>_amd64.deb and SHA256SUMS
set -euo pipefail

VERSION="${VERSION:-0.0.0-dev}"
PRESET="gcc-release"
OUT="artifacts/deb"
BIN="build/${PRESET}/src/cli/fileops"

test -x "${BIN}" || {
  echo "${BIN} not found; run 'make build PRESET=${PRESET}' first" >&2
  exit 1
}

# The release preset links fully statically (cmake/StaticBinary.cmake) so the package has no
# dependencies and no glibc floor. A dynamic binary built on the Ubuntu 24.04 image would need glibc
# 2.38 and fail to start on Debian 12 or Ubuntu 22.04, so this check is what keeps the install
# smoke test honest rather than a formality.
if objdump -p "${BIN}" | grep -qE '^\s+NEEDED'; then
  echo "binary is dynamically linked; FILEOPS_STATIC_BINARY must be ON for ${PRESET}:" >&2
  objdump -p "${BIN}" | grep -E '^\s+NEEDED' >&2
  exit 1
fi
echo "binary is fully static: $(file -b "${BIN}" | cut -d, -f1-3)"

rm -rf "${OUT}" && mkdir -p "${OUT}"
# The control file carries the Debian version (1.2.3~rc.1, so dpkg sorts it before 1.2.3) while the
# file name keeps the SemVer form: GitHub release assets cannot contain `~` and would be renamed,
# breaking the match with SHA256SUMS. Both are set in cmake/Packaging.cmake from FILEOPS_VERSION.
cpack --preset "${PRESET}"
# CPack's unpacked staging tree would otherwise travel with the artefact; only the .deb ships.
rm -rf "${OUT}/_CPack_Packages"

(
  cd "${OUT}"
  sha256sum ./*.deb >SHA256SUMS
  cat SHA256SUMS
)
dpkg-deb --field "${OUT}"/*.deb Package Version Architecture Depends
