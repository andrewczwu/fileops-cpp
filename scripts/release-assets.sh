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

# Fixed ownership and, when SOURCE_DATE_EPOCH is set, fixed timestamps keep the tarball reproducible.
tar -C "artifacts/stage" \
  --owner=0 --group=0 --numeric-owner \
  ${SOURCE_DATE_EPOCH:+--mtime="@${SOURCE_DATE_EPOCH}"} \
  -czf "${OUT}/fileops-cpp_${VERSION}_linux-x64.tar.gz" "${DIR}"

(
  cd "${OUT}"
  sha256sum ./*.deb ./*.tar.gz >SHA256SUMS
  cat SHA256SUMS
)
