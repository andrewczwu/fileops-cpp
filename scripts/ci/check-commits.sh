#!/usr/bin/env bash
# Enforce Conventional Commits (https://www.conventionalcommits.org) on a range of commits.
#
#   scripts/ci/check-commits.sh                # commits on this branch not yet on origin/main
#   scripts/ci/check-commits.sh <base>..<head> # explicit range
#   scripts/ci/check-commits.sh --message FILE # a single message (used by the commit-msg hook)
#
# Rules: `type(scope)!: subject` with a known type, subject ≤ 72 characters, no trailing period;
# body lines ≤ 100 characters. Merge commits and Dependabot's generated messages are accepted.
set -euo pipefail

TYPES='build|chore|ci|docs|feat|fix|perf|refactor|revert|style|test'
HEADER_RE="^(${TYPES})(\([a-z0-9._/-]+\))?!?: [^ ].{0,70}[^. ]$"

check_message() {
  local source="$1" message="$2" header body failed=0
  header="$(printf '%s\n' "${message}" | sed -n '1p')"
  body="$(printf '%s\n' "${message}" | sed '1,2d')"

  if [[ "${header}" =~ ^Merge\  ]] || [[ "${header}" =~ ^build\(deps ]]; then
    return 0
  fi
  if [[ ! "${header}" =~ ${HEADER_RE} ]]; then
    printf '::error::%s: header must be "type(scope)?: subject" (types: %s; ≤72 chars; no trailing period):\n    %s\n' \
      "${source}" "${TYPES//|/, }" "${header}" >&2
    failed=1
  fi
  if [[ -n "${body}" ]] && printf '%s\n' "${body}" | grep -qE '^.{101,}$'; then
    printf '::error::%s: body lines must be ≤100 characters\n' "${source}" >&2
    failed=1
  fi
  return "${failed}"
}

if [[ "${1:-}" == "--message" ]]; then
  check_message "commit message" "$(cat "${2:?usage: --message FILE}")"
  exit $?
fi

range="${1:-origin/main..HEAD}"
status=0
count=0
while IFS= read -r sha; do
  [[ -z "${sha}" ]] && continue
  count=$((count + 1))
  check_message "${sha:0:7}" "$(git log -1 --format=%B "${sha}")" || status=1
done < <(git rev-list --no-merges "${range}")

echo "checked ${count} commit message(s) in ${range}"
exit "${status}"
