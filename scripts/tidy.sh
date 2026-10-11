#!/usr/bin/env bash
# clang-tidy over every translation unit, driven by the compile_commands.json of a configured build.
# Checks and the warnings-as-errors policy live in .clang-tidy; headers under include/, src/ and
# tests/ are analysed with their includers.
#
#   scripts/tidy.sh build/<preset>      (the preset must be configured first: make configure)
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

BUILD_DIR="${1:?usage: tidy.sh build/<preset>}"
LLVM_MAJOR=18
test -f "${BUILD_DIR}/compile_commands.json" || {
  echo "${BUILD_DIR}/compile_commands.json not found; run 'make configure PRESET=<preset>' first" >&2
  exit 1
}

runner=""
for candidate in "run-clang-tidy-${LLVM_MAJOR}" "/opt/homebrew/opt/llvm@${LLVM_MAJOR}/bin/run-clang-tidy" \
  "/usr/local/opt/llvm@${LLVM_MAJOR}/bin/run-clang-tidy" run-clang-tidy; do
  if command -v "${candidate}" >/dev/null 2>&1; then
    runner="${candidate}"
    break
  fi
done
if [[ -z "${runner}" ]]; then
  echo "run-clang-tidy not found; use 'make docker-tidy' (the toolchain image ships clang-tidy ${LLVM_MAJOR})" >&2
  exit 1
fi

# Only the project's own sources; Conan-provided headers are system includes and are not checked.
"${runner}" -quiet -p "${BUILD_DIR}" "$(pwd)/src" "$(pwd)/tests"
echo "clang-tidy: no findings"
