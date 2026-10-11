# ADR 0004: CI runs in a pinned container and every step is a script

**Status:** accepted · **Date:** 2026-10-10

## Context

Pipelines commonly accumulate logic in YAML `run:` blocks and depend on whatever the hosted runner
image happens to contain. For C++ that is worse than usual: the compiler version, the standard
library and even the CMake release change results. Both make "works in CI but not locally" (and
vice versa) a routine failure, and make the pipeline impossible to exercise without pushing.

## Decision

- All build, test and packaging steps run inside `ghcr.io/andrewczwu/fileops-cpp-build`, built from
  `docker/Dockerfile.build` by its own workflow and rebuilt weekly for base-image patches. The image
  holds GCC 14, Clang 18 with clang-tidy and clang-format, CMake, Ninja, Conan 2.33.0, lintian and
  the Debian packaging tools, and an unprivileged `builder` user (uid 1001).
- Each multi-step task is a shell script in `scripts/` with a `Makefile` target; workflows call
  `make <target>` or the script directly. The YAML holds orchestration (job graph, caching, artefacts,
  summaries) only.
- Container jobs run as `--user 1001:1001` with `HOME=/home/builder`, and keep Conan's home inside
  the workspace (`scripts/ci/conan-cache-path.sh`) so `actions/cache` can save and restore it.
- The workflows are runnable offline with `act` (`.actrc`, `make act-<job>`); steps that need GitHub
  services are guarded and skipped locally.
- Third-party actions are pinned to commit SHAs; Dependabot keeps them current. Jobs declare
  least-privilege `permissions` and `timeout-minutes` per job, and `concurrency` per workflow.

## Reasons

- Same image locally (`make docker-test`) and in CI removes toolchain drift as a variable.
- `make test` reproducing the CI job is the fastest path from red build to fix.
- Scripts are testable, diff-able and reviewable; inline YAML shell is not.
- SHA pinning and least privilege are the current baseline for GitHub Actions supply-chain
  hygiene (`zizmor` enforces them).

## Consequences

- The toolchain image is a dependency that must itself be versioned. CI uses the `:main` tag: it is
  only ever written by this repository's own workflow from a pinned Dockerfile, and a tag reference
  is what lets `act` run the jobs against a locally built image without network access. The release
  workflow is meant to pin the image by digest (printed in the build-image job summary) once the
  first image is published, and resolves dependencies from scratch rather than from a cache so a
  poisoned cache entry can never reach a published artefact.
- Running as the unprivileged user is needed for permission tests, which costs a little setup
  (`HOME`, an explicit `CONAN_HOME`, a mounted cache directory for `make docker-*`).
- Each container job pays ~25 s to pull the image. The pipeline therefore has one container job per
  configuration that must be built (two sanitized Debug legs, one release job that also packages
  and lints) and nothing finer; clang-format and the other linters run on the host.
- `act` cannot run everything (OIDC, artifact API, code scanning), so some jobs are only proven on
  GitHub.
