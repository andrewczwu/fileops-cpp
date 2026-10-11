#!/usr/bin/env bash
# clang-format over every tracked C++ file, with the same major version everywhere.
#
#   scripts/format.sh --check   fail (with a diff) if any file is not formatted; what CI runs
#   scripts/format.sh --fix     rewrite files in place
#
# clang-format output differs between major versions, so only the pinned major is accepted:
# clang-format-18 on PATH, Homebrew's llvm@18, an unversioned clang-format that reports 18, or
# the toolchain image as a last resort. shfmt fixes shell scripts when --fix is given and it is installed.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

LLVM_MAJOR=18
BUILD_IMAGE="${BUILD_IMAGE:-ghcr.io/andrewczwu/fileops-cpp-build:main}"
mode="${1:-}"
case "${mode}" in
  --check | --fix) ;;
  *)
    echo "usage: scripts/format.sh --check|--fix" >&2
    exit 2
    ;;
esac

# (Written for bash 3.2, the version macOS ships, so no mapfile.)
files=()
while IFS= read -r -d '' f; do
  files+=("${f}")
done < <(git ls-files -z -- '*.cpp' '*.hpp')

reports_major() { "$1" --version 2>/dev/null | grep -qE "clang-format version ${LLVM_MAJOR}\."; }

clang_format=""
for candidate in "clang-format-${LLVM_MAJOR}" "/opt/homebrew/opt/llvm@${LLVM_MAJOR}/bin/clang-format" \
  "/usr/local/opt/llvm@${LLVM_MAJOR}/bin/clang-format" clang-format; do
  if command -v "${candidate}" >/dev/null 2>&1 && reports_major "${candidate}"; then
    clang_format="${candidate}"
    break
  fi
done

run_clang_format() {
  if [[ -n "${clang_format}" ]]; then
    "${clang_format}" "$@"
  else
    echo "clang-format ${LLVM_MAJOR} not found locally; using ${BUILD_IMAGE}" >&2
    docker run --rm --platform "${DOCKER_PLATFORM:-linux/amd64}" -v "${PWD}:/src" -w /src \
      "${BUILD_IMAGE}" clang-format "$@"
  fi
}

if [[ "${mode}" == "--fix" ]]; then
  run_clang_format -i "${files[@]}"
  if command -v shfmt >/dev/null 2>&1; then
    shfmt -w scripts .githooks
  fi
  echo "formatted ${#files[@]} C++ files"
else
  # --dry-run prints a diff-like report of every violation; -Werror turns it into a failure.
  run_clang_format --dry-run -Werror "${files[@]}"
  echo "clang-format: ${#files[@]} files clean"
fi
