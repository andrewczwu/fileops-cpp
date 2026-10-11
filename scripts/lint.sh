#!/usr/bin/env bash
# Run every non-C++ linter the repository uses. C++ formatting is covered by scripts/format.sh
# (clang-format) and static analysis by scripts/tidy.sh (clang-tidy); see Makefile `lint` and `tidy`.
#
# Each tool runs from a locally installed binary when one is on PATH, otherwise from its pinned
# Docker image, so a laptop and CI check the same rules with the same versions.
#
#   scripts/lint.sh            run everything, fail if any category has findings
#   scripts/lint.sh shell yaml run only the named categories
#   LINT_PREFER_DOCKER=1       ignore local binaries and always use the images (what CI does)
#   LINT_SKIP_MISSING=1        skip tools that are not installed locally instead of using Docker
#                              (used by the pre-commit hook to stay fast)
# The lint_* functions are dispatched by name from the category list at the bottom.
# shellcheck disable=SC2329
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

# Pinned image versions. Bump deliberately; Dependabot does not track these.
SHELLCHECK_IMAGE="koalaman/shellcheck:v0.11.0"
SHFMT_IMAGE="mvdan/shfmt:v3.14.1"
HADOLINT_IMAGE="hadolint/hadolint:v2.15.1"
YAMLLINT_IMAGE="pipelinecomponents/yamllint:0.35.13"
MARKDOWNLINT_IMAGE="davidanson/markdownlint-cli2:v0.23.3"
EC_IMAGE="mstruebing/editorconfig-checker:4.0.2"

status=0
run() {
  local name="$1"
  shift
  echo "::group::lint ${name}"
  if "$@"; then
    echo "ok   ${name}"
  else
    echo "FAIL ${name}" >&2
    status=1
  fi
  echo "::endgroup::"
}

# tool <binary> <image> <image-command> [args...]
# Runs the local binary when it is on PATH (unless LINT_PREFER_DOCKER is set), otherwise the pinned
# image. <image-command> is the executable to invoke inside images that define no ENTRYPOINT
# (hadolint, yamllint, editorconfig-checker); pass "" for images whose ENTRYPOINT is the tool.
tool() {
  local binary="$1" image="$2" image_command="$3"
  shift 3
  if [[ -z "${LINT_PREFER_DOCKER:-}" ]] && command -v "${binary}" >/dev/null 2>&1; then
    "${binary}" "$@"
  elif [[ -n "${LINT_SKIP_MISSING:-}" ]]; then
    echo "${binary} not installed; skipped (CI runs it from ${image})" >&2
  elif [[ -n "${image_command}" ]]; then
    docker run --rm -v "${PWD}:/work:ro" -w /work "${image}" "${image_command}" "$@"
  else
    docker run --rm -v "${PWD}:/work:ro" -w /work "${image}" "$@"
  fi
}

# Collect tracked files matching the given pathspecs into the global `files` array.
# (Written for bash 3.2, the version macOS ships, so no mapfile.)
files=()
collect() {
  files=()
  local f
  while IFS= read -r -d '' f; do
    files+=("${f}")
  done < <(git ls-files -z -- "$@")
}

# Categories that run more than one tool must aggregate explicitly: run() calls them inside an
# `if`, where bash suspends `set -e`, so only the last command's status would be seen otherwise.
lint_shell() {
  local rc=0
  collect '*.sh' .githooks
  tool shellcheck "${SHELLCHECK_IMAGE}" "" "${files[@]}" || rc=1
  # shfmt reads formatting options from .editorconfig; -d prints a diff and fails on differences.
  tool shfmt "${SHFMT_IMAGE}" "" -d "${files[@]}" || rc=1
  return "${rc}"
}

lint_docker() {
  collect 'docker/Dockerfile*'
  tool hadolint "${HADOLINT_IMAGE}" hadolint "${files[@]}"
}

lint_yaml() {
  tool yamllint "${YAMLLINT_IMAGE}" yamllint --strict .
}

lint_markdown() {
  tool markdownlint-cli2 "${MARKDOWNLINT_IMAGE}" "" '**/*.md'
}

lint_editorconfig() {
  tool editorconfig-checker "${EC_IMAGE}" editorconfig-checker
}

lint_workflows() {
  local rc=0
  if command -v actionlint >/dev/null 2>&1; then
    actionlint || rc=1
  else
    docker run --rm -v "${PWD}:/work:ro" -w /work rhysd/actionlint:1.7.12 -color || rc=1
  fi
  if command -v zizmor >/dev/null 2>&1; then
    zizmor --no-progress .github/workflows || rc=1
  else
    echo "zizmor not installed; CI runs it through zizmor-action" >&2
  fi
  return "${rc}"
}

lint_commits() {
  if git rev-parse --verify -q origin/main >/dev/null; then
    scripts/ci/check-commits.sh origin/main..HEAD
  else
    echo "no origin/main; skipping commit message check" >&2
  fi
}

categories=("$@")
if [[ ${#categories[@]} -eq 0 ]]; then
  categories=(shell docker yaml markdown editorconfig workflows commits)
fi
for category in "${categories[@]}"; do
  run "${category}" "lint_${category}"
done
exit "${status}"
