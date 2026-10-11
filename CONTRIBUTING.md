# Contributing

## Setup

```sh
git clone https://github.com/andrewczwu/fileops-cpp && cd fileops-cpp
make setup          # points git at .githooks and prints the tool list below
make test           # Conan install + configure + build + unit and integration tests
```

Requirements:

- A C++20 compiler: Apple Clang (macOS) or GCC 14 / Clang 18 (Linux). Only the Linux ones are
  used for shipped artefacts; macOS is a development loop.
- [CMake](https://cmake.org) 3.25 or newer, [Ninja](https://ninja-build.org), and
  [Conan](https://conan.io) 2 at the version the toolchain image pins: `pipx install conan==2.33.0`.
- GNU Make 3.82 or newer (macOS ships 3.81; `brew install make` and use `gmake`, or put its `gnubin`
  directory first on `PATH`).
- Docker, if you want to build the `.deb`, run the tests inside the CI image, or run the pipeline locally.

On macOS, `brew install cmake ninja make llvm@18 gitleaks actionlint zizmor shellcheck shfmt hadolint
yamllint editorconfig-checker act`, `npm i -g markdownlint-cli2` and `pipx install conan==2.33.0`
give you every tool the hooks and `make lint` use. Without them, `make lint` and `make format`
fall back to Docker images (clang-format comes from the toolchain image), but the pre-commit hook
silently skips the missing tools.

The first `make test` on a machine downloads the three dependencies and builds GoogleTest from
source into Conan's cache (`~/.conan2`); later builds reuse it.

## Formatting and style

Conventions are enforced, not suggested: every check below fails the build in CI and most run in the
pre-commit hook on the files you staged.

| What | Tool | Rules |
| --- | --- | --- |
| C++ layout | `scripts/format.sh` (clang-format, major version 18 only) | `.clang-format` |
| C++ static analysis | `scripts/tidy.sh` (clang-tidy, every enabled check is an error) | `.clang-tidy` |
| Compiler warnings | `-Wall -Wextra -Wpedantic -Wshadow -Wconversion …` with `-Werror` | `cmake/CompilerWarnings.cmake` |
| Memory and undefined behaviour | AddressSanitizer + UndefinedBehaviorSanitizer in every Debug preset | `cmake/Sanitizers.cmake` |
| Shell scripts and hooks | shellcheck, shfmt | `.shellcheckrc`, `[*.sh]` in `.editorconfig` |
| Dockerfiles | hadolint | `.hadolint.yaml` |
| YAML | yamllint | `.yamllint.yml` |
| Markdown | markdownlint-cli2 | `.markdownlint-cli2.yaml` |
| Whitespace, newlines, indentation (all files) | editorconfig-checker | `.editorconfig`, `.editorconfig-checker.json` |
| GitHub Actions workflows | actionlint, zizmor | — |
| Commit messages | `scripts/ci/check-commits.sh` (Conventional Commits) | `commit-msg` hook, CI on pull requests |
| Secrets | gitleaks | `.gitleaks.toml` |

`make lint` runs everything except clang-tidy; `make tidy` runs clang-tidy against the
`compile_commands.json` of the current preset (it configures first). `scripts/lint.sh` uses a locally
installed tool when one is on `PATH` and the pinned Docker image otherwise, so laptops and CI check
identical versions. clang-format output differs between major versions, which is why
`scripts/format.sh` accepts only version 18 (`clang-format-18`, Homebrew's `llvm@18`, or the image).
`make format` fixes C++ and shell formatting; the other tools report only.

## Workflow

1. Branch from `main`: `git switch -c feat/short-description`.
2. Make small commits using [Conventional Commits](https://www.conventionalcommits.org):
   `feat:`, `fix:`, `ci:`, `build:`, `docs:`, `test:`, `refactor:`, `chore:`, `perf:`, `revert:`,
   `style:`. The header is at most 72 characters with no trailing period; body lines at most 100.
3. Add or update tests. Behavioural changes must also assert on the telemetry they emit — see
   [docs/OBSERVABILITY.md](docs/OBSERVABILITY.md) for the event-id contract.
4. `make lint tidy test` locally, or `make docker-test` to run inside the exact CI image.
5. Open a pull request; the template has the checklist. CI must be green and history is kept linear
   (squash or rebase).

## Running the pipeline locally

```sh
docker pull ghcr.io/andrewczwu/fileops-cpp-build:main   # once (or: make build-image BUILD_IMAGE=ghcr.io/andrewczwu/fileops-cpp-build:main)
make act-test ACT_ARGS='--matrix preset:gcc-debug'       # the CI "Test (gcc-debug)" job, offline
make act-build                                           # the CI "Build, test & package (gcc-release)" job
make act-smoke ACT_ARGS='--matrix image:debian:bookworm-slim'
```

`make act-<job>` scopes `act` to `ci.yml` (every workflow has a job called `build`, so a bare
`act -j build` would run three of them). The container jobs resolve Conan dependencies from the
network, so this needs connectivity; `make test` and `make docker-test` work offline once the Conan
cache is warm (`make docker-*` mounts `~/.cache/fileops-cpp/conan2` into the container for that).
`.actrc` holds the remaining defaults. Steps that depend on GitHub-hosted services (artifact upload,
OIDC, code scanning) are skipped under `act` and marked as such in the workflow.

## Updating dependencies

Dependabot has no Conan ecosystem, so the three third-party packages are bumped by hand:

1. Change the version in `conanfile.py` (`cli11`, `nlohmann_json`, or the `gtest` test requirement).
2. Regenerate the lockfile for each profile the project builds with. `make lock` runs
   `conan lock create` for the current preset's profile, passing the existing lockfile in and out so
   the configurations merge into one file:

   ```sh
   make lock                             # macOS: the dev preset (Apple Clang)
   make docker-lock PRESET=gcc-debug     # Linux GCC 14, inside the toolchain image
   make docker-lock PRESET=clang-debug   # Linux Clang 18
   ```

3. `make test` under at least one preset, then commit `conanfile.py` and `conan.lock` together.

CI resolves every build with `--lockfile conan.lock`, so a version bump that forgets the lockfile
fails there rather than silently floating.

## Project layout

```text
include/fileops/        public headers: FileOperations, atomic_file, Error, ExitCode, telemetry contract
src/core/               libfileops_core: operations, atomic writer, log helpers (no CLI concerns)
src/cli/                CLI11 front end, TextSink/JsonSink, exit-code mapping
tests/support/          CollectingLogger, TempDirectory and helpers shared by both suites
tests/unit/             in-process tests: file-system outcomes + log records
tests/integration/      runs the real executable; parses its JSON log stream
cmake/                  CompilerWarnings, Sanitizers, StaticBinary, Packaging (CPack)
conan/profiles/         compiler-pinned Conan profiles
packaging/              man page, Debian copyright, changelog template
scripts/                every multi-step task CI runs, callable locally through the Makefile
docker/                 the pinned toolchain image
docs/                   product requirements, architecture, observability contract, decision records
```

## Decision records

Non-obvious choices live in [docs/adr](docs/adr). Add a new numbered file when you make one.
