#!/usr/bin/env bash
# Print the CONAN_HOME assignment for the current job (consumed via $GITHUB_ENV).
# The cache lives inside the workspace so actions/cache can save and restore it regardless of which
# user the container runs as or what HOME is set to. Under act the same path is used; dependencies
# are then resolved from the network, since act cannot add a host mount to a job that declares its
# own container options.
set -euo pipefail

echo "CONAN_HOME=${GITHUB_WORKSPACE:-$PWD}/.conan2"
