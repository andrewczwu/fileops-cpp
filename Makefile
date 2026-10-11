# Entry point for humans and CI alike. Every CI job calls a target defined here, so a green
# pipeline can always be reproduced locally with the same command.
#
#   make test                 configure, build and run unit + integration tests (default preset)
#   make test PRESET=clang-debug
#   make docker-test          the same, inside the pinned toolchain image as the same unprivileged user
#   make docker-package       release build (GCC 14, static C++ runtime) + .deb   (Linux only on the host)
#   make smoke                install the .deb into clean Debian/Ubuntu containers and exercise it
#   make ci                   lint, tidy, test, package, lintian, smoke in order (Linux with Docker)
#
# Presets (CMakePresets.json): dev (host compiler, Debug, sanitizers; works on macOS and Linux),
# gcc-debug / clang-debug (Linux, sanitizers), gcc-release (the shipped binary), clang-release.

# GNU Make 3.82 or newer is required: older versions silently ignore .SHELLFLAGS and would run
# recipes without `-euo pipefail`, letting a failing pipeline stage pass locally that fails in CI.
# macOS ships 3.81 (the last GPLv2 release); `brew install make` provides a current one as `gmake`.
ifneq ($(filter 3.80 3.81,$(MAKE_VERSION)),)
$(error GNU Make 3.82 or newer is required, found $(MAKE_VERSION). On macOS: brew install make, then use gmake or put "$$(brew --prefix make)/libexec/gnubin" first on PATH)
endif

SHELL := /usr/bin/env bash
.SHELLFLAGS := -euo pipefail -c
.DEFAULT_GOAL := help

VERSION   ?= 0.0.0-dev
ARTIFACTS := artifacts
BUILD_IMAGE ?= fileops-cpp-build:local
DOCKER_PLATFORM ?= linux/amd64
UNAME_S := $(shell uname -s)

# Default preset: the host compiler loop on macOS, GCC 14 with sanitizers on Linux.
ifeq ($(UNAME_S),Darwin)
PRESET ?= dev
else
PRESET ?= gcc-debug
endif

# Conan profile and build type follow from the preset. The profile pins the compiler so the
# dependency binaries Conan builds match what CMake links against.
ifeq ($(UNAME_S),Darwin)
PROFILE_dev := macos-apple-clang
else
PROFILE_dev := linux-gcc14
endif
PROFILE_gcc-debug := linux-gcc14
PROFILE_gcc-release := linux-gcc14
PROFILE_clang-debug := linux-clang18
PROFILE_clang-release := linux-clang18
PROFILE := $(PROFILE_$(PRESET))
BUILD_TYPE := $(if $(findstring release,$(PRESET)),Release,Debug)
BUILD_DIR := build/$(PRESET)

export FILEOPS_VERSION := $(VERSION)

# Run as the image's unprivileged user, exactly like CI, so permission tests execute rather than
# skip. A host directory is mounted as the Conan cache so repeated container builds are fast.
CONAN_CACHE ?= $(HOME)/.cache/fileops-cpp/conan2
DOCKER_RUN = docker run --rm --platform $(DOCKER_PLATFORM) \
	--user 1001:1001 -e HOME=/home/builder -e CONAN_HOME=/home/builder/.conan2 \
	-v "$(CURDIR)":/src -w /src \
	-v "$(CONAN_CACHE)":/home/builder/.conan2 \
	-e VERSION=$(VERSION) -e CI=$${CI:-} -e SOURCE_DATE_EPOCH=$${SOURCE_DATE_EPOCH:-} \
	$(BUILD_IMAGE)
# Only forward PRESET into the container when it was given explicitly; the container picks its own default.
ifeq ($(origin PRESET),command line)
DOCKER_PRESET := PRESET=$(PRESET)
endif

.PHONY: help setup deps lock configure build test lint format format-check tidy package release-assets smoke lint-deb clean build-image ci FORCE

help: ## Show this help
	@grep -E '^[a-zA-Z_%-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2}'

setup: ## One-time developer setup: git hooks and a note on local tools
	git config core.hooksPath .githooks
	@echo "Hooks installed (pre-commit: clang-format, lint staged files, scan for secrets; commit-msg: Conventional Commits)."
	@echo "Toolchain: cmake >= 3.25, ninja, conan 2 (pipx install conan==2.33.0), a C++20 compiler."
	@echo "Linters run from Docker images when not installed; for a faster local loop:"
	@echo "  brew install llvm@18 gitleaks actionlint zizmor shellcheck shfmt hadolint yamllint editorconfig-checker act && npm i -g markdownlint-cli2"

deps: ## Resolve dependencies with Conan into build/$(PRESET) (locked graph, builds what is missing)
	conan install . --output-folder $(BUILD_DIR) --build=missing --lockfile conan.lock \
		--profile:host conan/profiles/$(PROFILE) --profile:build conan/profiles/$(PROFILE) \
		--settings build_type=$(BUILD_TYPE)
	# Keep only the installed packages: the source, build and download folders Conan leaves behind
	# are several times their size (a Debug cache of this graph was 1.1 GB before, ~200 MB after),
	# and packages unused for a month are evicted so a dependency bump does not grow the cache forever.
	conan cache clean "*" --source --build --download
	conan remove "*" --lru=4w --confirm

lock: ## Add the current preset's profile to conan.lock (run for every profile after changing conanfile.py)
	conan lock create . $(if $(wildcard conan.lock),--lockfile conan.lock,) --lockfile-out conan.lock \
		--profile:host conan/profiles/$(PROFILE) --profile:build conan/profiles/$(PROFILE) \
		--settings build_type=$(BUILD_TYPE)

configure: deps ## Configure the CMake preset (version from VERSION, default 0.0.0-dev)
	cmake --preset $(PRESET)

build: configure ## Compile the preset
	cmake --build --preset $(PRESET)

test: build ## Unit + integration tests; JUnit report and CLI telemetry under artifacts/
	mkdir -p $(ARTIFACTS)/test-results
	FILEOPS_TEST_LOG_DIR=$(CURDIR)/$(ARTIFACTS)/logs/$(PRESET) \
		ctest --preset $(PRESET) --output-junit $(CURDIR)/$(ARTIFACTS)/test-results/$(PRESET).xml

lint: format-check ## clang-format, shell, Dockerfile, YAML, Markdown, editorconfig, workflows, commit messages, secrets
	scripts/lint.sh
	@if command -v gitleaks >/dev/null; then gitleaks git --no-banner --redact .; else echo "gitleaks not installed; CI scans the full history"; fi

format: ## Apply clang-format (and shfmt when installed) fixes
	scripts/format.sh --fix

format-check: ## Fail if any C++ file is not clang-format clean
	scripts/format.sh --check

tidy: configure ## clang-tidy over the whole tree using the preset's compile_commands.json
	scripts/tidy.sh $(BUILD_DIR)

package: ## Release build (gcc-release preset) with its tests, then the .deb → artifacts/deb/
	$(MAKE) test PRESET=gcc-release
	scripts/package.sh

release-assets: package ## Tarball (binary, licence, man page) and SHA256SUMS next to the .deb
	scripts/release-assets.sh

smoke: ## Install the .deb in clean Debian and Ubuntu containers and exercise every command
	scripts/smoke-test.sh

lint-deb: ## Run lintian against the built .deb
	scripts/lint-deb.sh

clean: ## Remove build output and artefacts
	rm -rf build $(ARTIFACTS) CMakeUserPresets.json

build-image: ## Build the pinned toolchain image used by CI and docker-* targets
	docker build --platform $(DOCKER_PLATFORM) -t $(BUILD_IMAGE) -f docker/Dockerfile.build docker

# Pattern rules cannot be declared .PHONY, so they depend on FORCE to always run.
docker-%: FORCE ## Run any target inside the build container, e.g. make docker-test, make docker-package
	mkdir -p "$(CONAN_CACHE)"
	$(DOCKER_RUN) make $* VERSION=$(VERSION) $(DOCKER_PRESET)

# Extra arguments for act, e.g. ACT_ARGS='--matrix preset:clang-debug'. Scoped to ci.yml because
# every workflow has a job called `build`. Container jobs resolve dependencies from the network:
# act cannot add a cache mount to a job that declares its own container `options`.
ACT_ARGS ?=
act-%: FORCE ## Run a ci.yml job locally with act, e.g. make act-test, make act-build, make act-smoke
	act -W .github/workflows/ci.yml -j $* $(ACT_ARGS)

FORCE:

ci: lint tidy test package lint-deb smoke ## Everything the pipeline runs, in order
