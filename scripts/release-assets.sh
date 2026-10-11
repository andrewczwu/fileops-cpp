#!/usr/bin/env bash
# Assemble the release assets next to the .deb: a tarball of the bare binary with its licence and
# man page under a versioned directory, and SHA256SUMS over everything.
# Inputs (env): VERSION (default 0.0.0-dev), SOURCE_DATE_EPOCH (optional)
set -euo pipefail

VERSION="${VERSION:-0.0.0-dev}"
OUT="artifacts/deb"
BIN="build/gcc-release/src/cli/fileops"
DIR="fileops-cpp-${VERSION}"
STAGE="artifacts/stage/${DIR}"

test -x "${BIN}" || {
  echo "${BIN} not found; run 'make package' first" >&2
  exit 1
}
compgen -G "${OUT}/*.deb" >/dev/null || {
  echo "no .deb in ${OUT}; run 'make package' first" >&2
  exit 1
}

rm -rf "${STAGE}" && mkdir -p "${STAGE}"
cp "${BIN}" "${STAGE}/fileops"
strip --strip-all "${STAGE}/fileops"
cp LICENSE packaging/fileops.1 "${STAGE}/"

# Reproducible bytes: fixed ownership, sorted entries, mtimes from SOURCE_DATE_EPOCH (the tagged
# commit in the release workflow), and gzip without its header timestamp (-n).
tar -C "artifacts/stage" \
  --sort=name --owner=0 --group=0 --numeric-owner \
  --mtime="@${SOURCE_DATE_EPOCH:-$(date +%s)}" \
  -cf - "${DIR}" | gzip -9 -n >"${OUT}/fileops-cpp_${VERSION}_linux-x64.tar.gz"

(
  cd "${OUT}"
  sha256sum ./*.deb ./*.tar.gz >SHA256SUMS
  cat SHA256SUMS
)
