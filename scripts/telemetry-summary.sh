#!/usr/bin/env bash
# Turn the NDJSON log stream captured by the integration tests into a Markdown report.
# Used by the CI build job to append telemetry evidence to the job summary, so an engineer (or an
# AI assistant reading the uploaded NDJSON) can see what the tool did without re-running tests.
#
# Usage: scripts/telemetry-summary.sh artifacts/logs/cli-runs.ndjson
set -euo pipefail

FILE="${1:-artifacts/logs/cli-runs.ndjson}"
if [[ ! -s "${FILE}" ]]; then
  echo "_No telemetry captured (${FILE} missing or empty)._"
  exit 0
fi

runs=$(jq -c 'select(.run)' "${FILE}" | wc -l | tr -d ' ')
records=$(jq -c 'select(.EventId)' "${FILE}" | wc -l | tr -d ' ')
failures=$(jq -c 'select(.run and .run.exitCode != 0)' "${FILE}" | wc -l | tr -d ' ')

cat <<EOF
### CLI telemetry captured during integration tests

| Invocations | Log records | Non-zero exits (expected by tests) |
|---:|---:|---:|
| ${runs} | ${records} | ${failures} |

#### Log records by event

| EventId | Level | Count | Example message |
|---:|---|---:|---|
EOF
jq -r 'select(.EventId) | [.EventId, .LogLevel, .Message] | @tsv' "${FILE}" \
  | awk -F'\t' '{ key=$1 "\t" $2; count[key]++; if (!(key in example)) example[key]=$3 } END { for (k in count) print k "\t" count[k] "\t" example[k] }' \
  | sort -n \
  | awk -F'\t' '{ printf "| %s | %s | %s | `%s` |\n", $1, $2, $3, $4 }'

cat <<EOF

#### Exit codes observed

| Exit code | Count |
|---:|---:|
EOF
jq -r 'select(.run) | .run.exitCode' "${FILE}" | sort -n | uniq -c | awk '{ printf "| %s | %s |\n", $2, $1 }'

errors=$(jq -c 'select(.LogLevel == "Error" or .LogLevel == "Critical")' "${FILE}" | head -n 20)
if [[ -n "${errors}" ]]; then
  cat <<EOF

<details><summary>Error-level records (first 20)</summary>

\`\`\`json
${errors}
\`\`\`

</details>
EOF
fi
