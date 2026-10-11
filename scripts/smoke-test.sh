#!/usr/bin/env bash
# Install the .deb into pristine Debian-family containers and exercise every command.
# This is the "does it actually install on a Debian system" proof, run on each release.
# Inputs (env): SMOKE_IMAGES (space-separated, default debian:bookworm-slim ubuntu:24.04), DOCKER_PLATFORM
set -euo pipefail

if ! compgen -G "artifacts/deb/*.deb" >/dev/null; then
  echo "no .deb in artifacts/deb; run 'make package' first" >&2
  exit 1
fi
PLATFORM="${DOCKER_PLATFORM:-linux/amd64}"
IMAGES="${SMOKE_IMAGES:-debian:bookworm-slim ubuntu:24.04}"

read -r -d '' SCRIPT <<'EOF' || true
set -eu
# Prefer apt so dependency resolution is exercised; fall back to dpkg for images without package lists.
apt-get install -y --no-install-recommends /pkg/*.deb >/dev/null 2>&1 || dpkg -i /pkg/*.deb
echo "installed: $(dpkg-query -W -f='${Package} ${Version} ${Architecture}' fileops-cpp)"
fileops --version

cd "$(mktemp -d)"
fileops create a.txt --text "hello "
fileops create b.txt --text "world"
fileops copy a.txt c.txt
fileops combine c.txt b.txt out.txt
test "$(cat out.txt)" = "hello world"
fileops delete out.txt
# A pipeline starting with `!` is exempt from `set -e`, so negative checks must exit explicitly.
if test -e out.txt; then echo "delete left out.txt behind" >&2; exit 1; fi

# Exit codes are part of the contract; make sure the packaged binary honours them.
fileops copy a.txt c.txt && exit 1 || test $? -eq 3
fileops delete nope.txt  && exit 1 || test $? -eq 2

# Logging works in the packaged build and goes to stderr only.
test -z "$(FILEOPS_LOG_LEVEL=Information FILEOPS_LOG_FORMAT=json fileops copy a.txt d.txt 2>/dev/null)"
FILEOPS_LOG_LEVEL=Information FILEOPS_LOG_FORMAT=json fileops copy a.txt e.txt 2>&1 >/dev/null | grep -q '"EventId":3001'

# Docker base images tell dpkg to skip /usr/share/man and /usr/share/doc (path-exclude), so check
# the package manifest rather than the installed tree for the documentation files.
dpkg-deb -c /pkg/*.deb 2>/dev/null | grep -q ' \./usr/share/man/man1/fileops\.1\.gz$'
dpkg-deb -c /pkg/*.deb 2>/dev/null | grep -q ' \./usr/share/doc/fileops-cpp/copyright$'
dpkg-deb -c /pkg/*.deb 2>/dev/null | grep -q ' \./usr/share/doc/fileops-cpp/changelog\.gz$'

dpkg -r fileops-cpp >/dev/null
# dash caches command locations, so `command -v` would still find the binary; check the file and dpkg.
hash -r 2>/dev/null || true
if test -e /usr/bin/fileops || dpkg -s fileops-cpp >/dev/null 2>&1; then echo "fileops still installed after removal" >&2; exit 1; fi
echo "removed cleanly"
EOF

status=0
for image in ${IMAGES}; do
  echo "::group::smoke ${image}"
  if docker run --rm --platform "${PLATFORM}" -v "$(pwd)/artifacts/deb:/pkg:ro" "${image}" sh -c "${SCRIPT}"; then
    echo "PASS ${image}"
  else
    echo "FAIL ${image}"
    status=1
  fi
  echo "::endgroup::"
done
exit "${status}"
