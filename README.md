# fileops-cpp

[![CI](https://github.com/andrewczwu/fileops-cpp/actions/workflows/ci.yml/badge.svg)](https://github.com/andrewczwu/fileops-cpp/actions/workflows/ci.yml)
[![Release](https://github.com/andrewczwu/fileops-cpp/actions/workflows/release.yml/badge.svg)](https://github.com/andrewczwu/fileops-cpp/actions/workflows/release.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A command-line tool that creates, copies, combines and deletes files — with atomic writes,
safe-by-default overwrite handling, stable exit codes, and built-in structured logging that the
test suite and CI treat as evidence.

Built with C++20, CMake and Conan; the point of this repository is to show disciplined delivery
applied to a native C++ toolchain. Ships as a single, fully static Linux executable with no
run-time dependencies, packaged as a `.deb`.

```text
$ fileops create notes.txt --text "hello"
$ fileops copy notes.txt backup.txt
$ fileops combine notes.txt backup.txt both.txt
$ fileops delete backup.txt
$ fileops copy notes.txt both.txt
fileops: file already exists: both.txt (use --force to overwrite)
$ echo $?
3
```

## Install

Download the `.deb` from the [latest release](https://github.com/andrewczwu/fileops-cpp/releases/latest):

```sh
VERSION=1.0.0   # see the releases page for the current version
curl -LO "https://github.com/andrewczwu/fileops-cpp/releases/download/v${VERSION}/fileops-cpp_${VERSION}_amd64.deb"
sudo apt install "./fileops-cpp_${VERSION}_amd64.deb"
fileops --version
man fileops
```

Works on Debian 12 and 13 and Ubuntu 22.04 and 24.04 (x86-64), and on any other x86-64 Linux. The
package installs `/usr/bin/fileops` and declares no dependencies: the binary is linked fully
statically (libstdc++, libgcc and glibc), which `scripts/package.sh` verifies against the ELF file
before packaging. The reasoning is in [ADR 0005](docs/adr/0005-compilers-warnings-and-sanitizers.md).

Every release also ships `fileops-cpp_<version>_linux-x64.tar.gz` (the binary, `LICENSE` and the man
page under `fileops-cpp-<version>/`), a `SHA256SUMS` file, and a
[SLSA build provenance attestation](https://docs.github.com/en/actions/security-for-github-actions/using-artifact-attestations)
covering all three. Verify that an asset was built by this repository's release workflow, not
merely signed by someone with access to the repository:

```sh
gh attestation verify "fileops-cpp_${VERSION}_amd64.deb" --owner andrewczwu \
  --signer-workflow andrewczwu/fileops-cpp/.github/workflows/release.yml
```

Remove with `sudo apt remove fileops-cpp`.

## Usage

```text
fileops [--log-level LEVEL] [--log-format text|json] [-v] <command> [args]

  create  <path> [--text TEXT] [--force]      Create a file, empty or with text (UTF-8, as given)
  copy    <source> <destination> [--force]    Copy a file, preserving permissions
  combine <first> <second> <output> [--force] Concatenate two files into a third
  delete  <path> [--force]                    Delete a file (--force: ok if missing, like rm -f)
```

Rules that hold for every command:

- An existing destination is **never overwritten** unless `--force` is given; when it is,
  `create` and `combine` keep the existing file's permission bits and `copy` applies the source's.
- Writes are **atomic**: content goes to a temporary file beside the destination and is renamed
  (or hard-linked, when not overwriting) into place, so the destination is never left partially
  written. A consequence: `combine a b a --force` safely appends `b` to `a`. (If the process is
  killed by a signal mid-write, a `.name.<id>.tmp` sibling can remain; the destination itself is
  untouched.)
- `copy` creates the destination with the source's permission bits passed to `open(2)`, so the copy
  is never more readable than the original, not even for an instant.
- **stdout is reserved for results** (only `--help` and `--version` use it); diagnostics go to stderr.
  A usage error prints one `fileops: <message>` line per error plus
  `fileops: run 'fileops --help' for usage` on stderr, exits 1 and writes nothing to stdout.
- Directories are refused; copying a file onto itself is refused; empty paths are usage errors.

| Exit code | Meaning |
| ---: | --- |
| 0 | success |
| 1 | usage error (invalid arguments, empty path, same source and destination, path is a directory) |
| 2 | an input file or directory does not exist |
| 3 | destination exists and `--force` was not given |
| 4 | the file system refused the operation (permission denied, disk full, locked file) |
| 70 | internal error |

## Telemetry

Logging is off by default. Turn it on per run or via environment variables:

```sh
$ fileops -v copy a b                       # human-readable, Information level, UTC times
16:48:15.060 info: fileops.core.FileOperations[3001] Copied a to b (1 bytes)
16:48:15.061 info: fileops.core.FileOperations[1001] Copy completed on b: 1 bytes in 0.81 ms

$ FILEOPS_LOG_FORMAT=json FILEOPS_LOG_LEVEL=Debug fileops copy a b 2>run.ndjson
$ jq -c '{EventId, EventName, LogLevel, State}' run.ndjson
{"EventId":1000,"EventName":"OperationStarted","LogLevel":"Debug","State":{"Operation":"Copy","Path":"b"}}
{"EventId":3001,"EventName":"FileCopied","LogLevel":"Information","State":{"Source":"a","Destination":"b","Bytes":1}}
{"EventId":1001,"EventName":"OperationCompleted","LogLevel":"Information","State":{"Operation":"Copy","Path":"b","Bytes":1,"DurationMs":0.81}}
```

Each JSON record is one line with the fields `Timestamp` (ISO 8601, UTC), `EventId`, `EventName`,
`LogLevel`, `Category`, `Message` and `State`. Event ids and field names are a documented contract:
[docs/OBSERVABILITY.md](docs/OBSERVABILITY.md).

Every operation also produces one OpenTelemetry span and three metrics. Set
`OTEL_EXPORTER_OTLP_ENDPOINT` and they are exported over OTLP/HTTP, together with log records:
Information and above by default (the console stays silent), or whatever `--log-level` says, which
governs every sink. Span tags and instruments are part of the documented contract.

```sh
docker run -d -p 4318:4318 otel/opentelemetry-collector
OTEL_EXPORTER_OTLP_ENDPOINT=http://localhost:4318 fileops copy a b
```

## Build from source

Requirements: a C++20 compiler (GCC 14, Clang 18 or Apple Clang), [CMake](https://cmake.org) 3.25
or newer, [Ninja](https://ninja-build.org), [Conan](https://conan.io) 2.33, GNU Make 3.82 or newer,
and Docker for the Linux artefacts (the `.deb` is built in the toolchain image) and the install
smoke test. In one line: CMake describes the build, Ninja runs it, Conan fetches the third-party
libraries (CLI11, nlohmann_json, opentelemetry-cpp with its OTLP/HTTP exporter, GoogleTest) at the
versions pinned in `conan.lock`. The first dependency build compiles protobuf, OpenSSL and libcurl
and takes some minutes; the Conan cache (and CI's cache of it) makes every later build quick.

```sh
make test                      # Conan install, configure, build, unit + integration tests (default preset)
make test PRESET=clang-debug   # the same under Clang 18 with sanitizers (Linux)
make tidy                      # clang-tidy over the whole tree
make lint                      # clang-format check, shell, Dockerfile, YAML, Markdown, workflows, secrets
make format                    # apply clang-format (and shfmt) fixes
make docker-test               # the tests inside the pinned CI toolchain image, as its unprivileged user
make docker-package            # release build + .deb  → artifacts/deb/
make smoke                     # install the .deb in clean Debian/Ubuntu containers and exercise it
make lint-deb                  # lintian
make help                      # everything else
```

Builds are described by [CMake presets](CMakePresets.json), each with its own directory under
`build/<preset>/` and a matching Conan profile under `conan/profiles/`:

| Preset | Compiler | Build type | Notes |
| --- | --- | --- | --- |
| `dev` | host compiler (Apple Clang on macOS, GCC 14 in the image) | Debug | sanitizers on; the default on macOS |
| `gcc-debug` | GCC 14 | Debug | AddressSanitizer + UndefinedBehaviorSanitizer; the default on Linux |
| `clang-debug` | Clang 18 | Debug | sanitizers; CI also runs clang-tidy here |
| `gcc-release` | GCC 14 | Release | fully static link; **the shipped binary**; the only packaging preset |
| `clang-release` | Clang 18 | Release | second-compiler release check |

Sanitizers are compiler instrumentation that makes memory errors and undefined behaviour abort the
test instead of passing silently; they are on in every Debug preset. `make build-image` builds the
toolchain image locally if you do not want to pull it from GHCR.

## Testing

Three layers, all run by CI; `make test` runs the first two, `make smoke` the third:

| Layer | What runs | What it proves |
| --- | --- | --- |
| Unit (`tests/unit`, 59 tests) | `fileops::core` in-process with a `CollectingLogger` capturing every record | each operation's file-system behaviour, atomicity, error-to-exit-code mapping, **and** that the right log events with the right fields were emitted |
| Integration (`tests/integration`, 40 tests) | the real `fileops` executable as a child process, with JSON logging on | argument parsing, exit codes, stdout/stderr discipline, log schema, and that logged facts (bytes, exit code, reason) match what actually happened |
| Package smoke (`scripts/smoke-test.sh`) | the installed `.deb` in pristine `debian:bookworm-slim` (12), `debian:trixie-slim` (13), `ubuntu:22.04`, `ubuntu:24.04` containers (CI; `make smoke` defaults to bookworm and 24.04, override with `SMOKE_IMAGES`) | it installs, runs, honours exit codes, logs, ships a man page, and removes cleanly |

The 99 tests run under both compilers with sanitizers, and again in the release configuration
whose binary goes into the package — so the artefact that ships is the artefact that was tested.
Every CLI invocation's log stream is captured to `cli-runs.ndjson`, summarised into the job summary,
and uploaded; a red build can be diagnosed from that file without a re-run. The `diagnose` job in
`ci.yml` hands that file to an AI assistant on failing same-repository pull requests; it is dormant
until an `ANTHROPIC_API_KEY` repository secret exists.

## Pipeline

```text
PR / push to main ──► ci.yml
   format        clang-format 18 on the runner                                ─┐
   secrets       gitleaks over full history                                    ├─ parallel
   lint          shellcheck, shfmt, hadolint, yamllint, markdownlint,         ─┘
                 editorconfig-checker, actionlint, zizmor, Conventional Commits
   codeql        C++ analysis (GitHub default setup; weekly and on pull requests)
   test ×2       toolchain container: gcc-debug and clang-debug, ASan + UBSan,
                 clang-tidy on the clang leg, telemetry → job summary
   build         toolchain container: gcc-release build → tests → CPack .deb → lintian
   smoke ×4      apt install in clean containers, exercise, remove
   diagnose      (on failure, same-repo PRs, dormant without a secret) AI summary from the telemetry

tag vX.Y.Z ──► release.yml
   build (sanitizer tests → release build → .deb → lintian → tarball + SHA256SUMS)
   → verify ×4 → provenance attestation → GitHub release
   (a pre-release suffix such as v1.2.0-rc.1 publishes a GitHub pre-release)

docker/Dockerfile.build ──► build-image.yml (on change + weekly) ──► ghcr.io/andrewczwu/fileops-cpp-build
```

Design choices worth knowing (details in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[docs/adr](docs/adr)):

- **One toolchain for everything that builds.** Both compilers, CMake, Ninja, Conan, lintian and
  the Debian tools live in one pinned container image; tests, packaging and lintian run there as an
  unprivileged user, and `make docker-test` runs the identical thing locally. clang-format and the
  other linters run on the host because they need no toolchain.
- **Scripts, not YAML.** Every multi-step job calls a script under `scripts/`, usually through a
  Makefile target, so pipeline logic is reviewable and runnable without pushing.
- **Runnable locally.** `make act-test` and `make act-build` execute the real CI jobs with `act`
  (`.actrc` has the defaults; the Makefile target scopes it to `ci.yml`).
- **Reproducible dependencies.** `conan.lock` pins the full resolved graph and every `conan install`
  uses `--lockfile`; the Conan profiles pin the compiler version so dependency binaries match what
  CMake links against; `[platform_tool_requires]` tells Conan the platform's CMake satisfies
  GoogleTest's build requirement so it never downloads its own.
- **Supply-chain hygiene.** Actions pinned to commit SHAs with Dependabot updates on a seven-day
  cooldown (actions and the Docker base image), least-privilege `permissions` and `timeout-minutes`
  on every job and `concurrency` on every workflow, secret scanning in a pre-commit hook and in CI,
  CodeQL (default setup), a tag ruleset on `v*`, immutable releases, SLSA provenance on releases.
- **Version from the tag.** `release.yml` derives the version from `vX.Y.Z` and refuses to run if
  `CHANGELOG.md` has no matching section.

## Releasing

```sh
# 1. add a "## [1.2.3] - YYYY-MM-DD" section to CHANGELOG.md and merge it
# 2.
git tag -a v1.2.3 -m "fileops-cpp 1.2.3" && git push origin v1.2.3
```

The release workflow does the rest. A tag with a pre-release suffix (`v1.2.3-rc.1`) publishes a
GitHub pre-release and may reuse the `[1.2.3]` changelog section, which is how the release path is
exercised before a final version. CI builds use `0.0.0-ci.N`; local builds use `0.0.0-dev`.

## Non-goals

Choices made on purpose for a single-maintainer tool with one requested target:

- no apt repository, RPM, Homebrew tap, container image or `curl | sh` installer — `apt install ./file.deb`
  from a verified release is the supported path;
- no arm64 or musl builds — x86-64 Debian/Ubuntu is the brief; the glibc-static binary already runs
  on every supported distribution without a second libc toolchain;
- no merge queue, reusable workflows, SLSA Build L3 builder or SBOM — each adds machinery without a
  consumer here; provenance attestations give Build L2 today;
- no coverage gate, mutation testing or fuzzing — 99 deterministic tests under two sanitized
  compilers on a tool that parses only its own arguments;

## Repository layout

```text
include/fileops/         public headers: operations, atomic writer, error, exit codes, telemetry contract
src/core/                libfileops_core: the operations, atomic writes, log helpers (no CLI code)
src/cli/                 CLI11 front end, text/JSON log sinks, exit-code mapping
tests/support/           CollectingLogger, TempDirectory
tests/unit/              in-process tests of fileops::core
tests/integration/       spawns the real binary; parses its JSON log stream
cmake/                   warnings, sanitizers, static linking, CPack configuration
conan/profiles/          compiler-pinned Conan profiles (linux-gcc14, linux-clang18, macos-apple-clang)
packaging/               man page, Debian copyright and changelog template
scripts/                 build, package, smoke-test, lint, CI helpers
docker/                  toolchain image
docs/                    product requirements, architecture, observability contract, decision records
.github/                 workflows, Dependabot, CODEOWNERS, PR template
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for the development workflow and
[SECURITY.md](SECURITY.md) for the security posture.
