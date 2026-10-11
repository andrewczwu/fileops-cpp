#!/usr/bin/env bash
# Run lintian (Debian's package checker) against the built .deb, failing on warnings and errors.
# Uses the lintian on PATH when there is one (the toolchain image ships it); otherwise runs it in a
# container so the host does not need Debian tooling.
set -euo pipefail

if ! compgen -G "artifacts/deb/*.deb" >/dev/null; then
  echo "no .deb in artifacts/deb; run 'make package' first" >&2
  exit 1
fi

LINTIAN_ARGS=(--fail-on warning --no-tag-display-limit --info)

if command -v lintian >/dev/null 2>&1; then
  lintian "${LINTIAN_ARGS[@]}" artifacts/deb/*.deb
  echo "lintian: no warnings or errors"
  exit 0
fi

PLATFORM="${DOCKER_PLATFORM:-linux/amd64}"
LINT_IMAGE="${LINT_IMAGE:-fileops-cpp-build:local}"

if ! docker image inspect "${LINT_IMAGE}" >/dev/null 2>&1; then
  docker build --platform "${PLATFORM}" -t "${LINT_IMAGE}" -f docker/Dockerfile.build docker
fi

docker run --rm --platform "${PLATFORM}" -v "$(pwd)/artifacts/deb:/pkg:ro" "${LINT_IMAGE}" \
  lintian "${LINTIAN_ARGS[@]}" /pkg/*.deb
echo "lintian: no warnings or errors"
