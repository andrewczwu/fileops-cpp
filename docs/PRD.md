# fileops-cpp — product requirements

| | |
| --- | --- |
| Product | `fileops`, a command-line tool for creating, copying, combining and deleting files (C++ implementation, package `fileops-cpp`) |
| Status | In development (first release pending) |
| Owner | Andrew Wu |
| Last updated | 2026-10-10 |

## 1. Purpose

`fileops` gives scripts and operators four file operations that are safe by default, predictable
under failure, and observable. It exists to show, in a small and fully verifiable package, how a
native C++ command-line tool should be built, tested, packaged and released for Debian-based Linux
systems.

The functional surface is deliberately small. The product's value is in the guarantees around
each operation — nothing is overwritten by accident, nothing is left half-written, every outcome
has a stable exit code and a machine-readable log record — and in a delivery pipeline whose every
claim is enforced rather than asserted.

## 2. Background

The original brief asked for a command-line tool, in any language, with commands to create a file
(empty or with text), copy a file, combine two files into a third, and delete a file; with unit and
integration tests, documentation, and clear instructions for building, testing and installing;
delivered as a single file that installs on a Debian-based Linux system; structured as if it were a
larger project. Evaluation criteria were CI/CD practice, use of version control, and the technical
quality of the implementation.

A first implementation in C# ([andrewczwu/fileops](https://github.com/andrewczwu/fileops)) is
shipped. This repository is the same product built with the toolchain the brief suggested — C++
and CMake — keeping the command-line contract, exit codes and log schema identical so that the two
are interchangeable for scripts and log consumers. Everything beyond the four commands is a
decision about *how well* to meet the brief, recorded in [docs/adr](adr).

## 3. Users

| User | Needs |
| --- | --- |
| Script author | Compose the commands in shell scripts; branch on exit codes; never parse human-readable output |
| Operator at a terminal | Clear usage text, a man page, errors that say what to do, no surprises with existing files |
| Platform or release engineer | Install from a single verifiable package; confirm what was built, by what, from which commit |
| Reviewer of the codebase | Understand the design in minutes; see that every stated guarantee is tested and enforced in CI |

## 4. Goals and non-goals

### Goals

1. Four correct operations with conservative defaults.
2. Atomic writes: a destination is complete or untouched, never partial.
3. A stable contract for scripts: exit codes, stdout/stderr discipline, structured logs.
4. Observability built in, and used as test evidence rather than decoration.
5. One fully static native executable with no run-time dependencies, packaged as a `.deb` that
   installs on Debian 12 and 13 and Ubuntu 22.04 and 24.04.
6. A release that a consumer can verify end to end: checksums, build provenance, immutability.
7. A pipeline that is fast, reproducible locally, and fails whenever a guarantee is broken —
   including memory errors and undefined behaviour, which C++ makes possible and sanitizers make visible.

### Non-goals

- Directory operations, recursion, globbing, moving or renaming.
- Interactive prompts or confirmation dialogs.
- Windows or macOS as supported runtime targets (the code builds on macOS for development; packages
  are Linux x86-64).
- An apt repository, RPM, Homebrew tap, container image or `curl | sh` installer.
- arm64 or musl builds, until a consumer asks for them.
- Configuration files; behaviour is controlled by flags and environment variables only.

## 5. Functional requirements

### 5.1 Commands

| Command | Behaviour |
| --- | --- |
| `create <path> [--text TEXT] [--force]` | Create `path`, empty or containing `TEXT` written as UTF-8 exactly as given (no trailing newline). |
| `copy <source> <destination> [--force]` | Copy the bytes of `source` to `destination`, preserving the source's Unix permission bits. |
| `combine <first> <second> <output> [--force]` | Write the bytes of `first` followed by the bytes of `second` to `output`, with no separator. |
| `delete <path> [--force]` | Delete `path`. With `--force`, a missing file is not an error (`rm -f` semantics). |

Global options on every command: `--log-level`, `--log-format`, `-v/--verbose`, `--help` and
`--version`; they may appear before or after the subcommand.

### 5.2 Rules that hold for every command

| Rule | Detail |
| --- | --- |
| Never overwrite silently | An existing destination is an error (exit 3) unless `--force` is given. |
| Atomic writes | Content goes to a temporary file in the destination directory, is flushed to disk, and is renamed (or hard-linked when not overwriting) into place; the destination is never partially written. A process killed by a signal may leave a `.name.<id>.tmp` sibling, never a damaged destination. |
| Permission bits | `copy` creates the destination with the source's bits passed to `open(2)`, so the copy is never more readable than the original, even for an instant. With `--force`, `create` and `combine` keep the existing file's bits; `copy` applies the source's. |
| Inputs are read fully before output is renamed | So `combine a b a --force` appends `b` to `a` safely, and `combine a a a --force` doubles `a`. |
| Directories are refused | Every path argument must name a file; a directory is a usage error (exit 1). |
| Same-file copies are refused | `copy` onto the same inode (including through `.` or symbolic links) is a usage error. |
| Empty arguments are usage errors | `""` or whitespace for any path exits 1 with the normal failure telemetry. |
| Binary-safe | All content is treated as bytes; no encoding, newline or BOM handling except for `--text`, which is UTF-8. |
| Large files | Content is streamed in 1 MiB chunks; file size is bounded by disk, not memory. |
| Long names | File names up to the file-system limit (255 bytes) work; the temporary name is shortened on a UTF-8 character boundary to fit. |

### 5.3 Output contract

| Stream | Content |
| --- | --- |
| stdout | Nothing, except `--help` and `--version`; a usage error writes nothing to stdout. Reserved so the tool composes in pipelines. |
| stderr | Optional structured log records, then on failure one line: `fileops: <message>`. A usage error prints one `fileops: <message>` line per error plus `fileops: run 'fileops --help' for usage`. |

### 5.4 Exit codes

| Code | Meaning |
| ---: | --- |
| 0 | Success |
| 1 | Usage error: invalid arguments, empty path, same source and destination, path is a directory |
| 2 | A required input file or directory does not exist |
| 3 | The destination exists and `--force` was not given |
| 4 | The file system refused the operation (permission denied, disk full, locked file) |
| 70 | Internal error (`EX_SOFTWARE`) |

Exit codes are part of the public contract and are covered by integration tests against the real
executable and against the installed package.

## 6. Observability requirements

Telemetry is a documented contract ([docs/OBSERVABILITY.md](OBSERVABILITY.md)), not a by-product.

| Requirement | Detail |
| --- | --- |
| Structured logs | Every operation emits start, complete or fail records with stable event ids (1xxx lifecycle, 2xxx create, 3xxx copy, 4xxx combine, 5xxx delete) and event names, with named fields (paths, bytes, duration, exit code, reason). |
| Formats | Human-readable text (UTC times) or one JSON object per line (`--log-format json`), always on stderr. Off by default; enabled by flag or `FILEOPS_LOG_LEVEL` / `FILEOPS_LOG_FORMAT`. |
| Schema stability | The JSON record shape `{Timestamp, EventId, EventName, LogLevel, Category, Message, State}` and the event ids are identical to the C# tool's, so one log consumer serves both. |
| Traces | One span per operation (`fileops.<operation>`) with path, size, overwrite and exit-code tags; status `Error` with the user-facing message on failure. |
| Metrics | Operation counter, duration histogram and bytes counter, tagged by operation and outcome. |
| Export | When `OTEL_EXPORTER_OTLP_ENDPOINT` is set, traces, metrics and logs are exported over OTLP/HTTP; standard `OTEL_*` variables are honoured. Without `--log-level` the exporter receives Information and above while the console stays silent; `--log-level` governs every sink. |
| Flush guarantee | Sinks write synchronously and the exporters are flushed before the summary line; all telemetry precedes the summary line and the process exit. |
| Evidence | A logged failure's exit code equals the process exit code; a logged byte count equals what is on disk. Tests assert this. |
| Host independence | The core library depends only on an abstract `Logger` interface; sinks live in the CLI and the tests. |

## 7. Delivery requirements

| Requirement | Detail |
| --- | --- |
| Artefact | One native Linux x86-64 executable built with GCC 14 at `-O2` and linked fully statically (libstdc++, libgcc, glibc), so it has no run-time dependencies and no glibc version floor. `scripts/package.sh` refuses to package a binary with any dynamic `NEEDED` entry. |
| Package | `fileops-cpp_<version>_amd64.deb` built by CPack, installing `/usr/bin/fileops`, `/usr/share/man/man1/fileops.1.gz`, `/usr/share/doc/fileops-cpp/copyright` and `changelog.gz`; lintian-clean at warning level. |
| Installation | `apt install ./fileops-cpp_<version>_amd64.deb` on Debian 12/13 and Ubuntu 22.04/24.04; `apt remove fileops-cpp` removes it cleanly. Verified on each release by installing in pristine containers. |
| Additional assets | `fileops-cpp_<version>_linux-x64.tar.gz` (stripped binary, licence, man page) and `SHA256SUMS`. |
| Verifiability | SLSA build provenance attestation for every asset, verifiable with `gh attestation verify --signer-workflow`; releases are immutable once published; release tags cannot be moved or deleted. |
| Versioning | Semantic Versioning. The git tag is the single source of truth, passed to CMake as `FILEOPS_VERSION`; a release refuses to run without a matching `CHANGELOG.md` section. Pre-release tags publish as pre-releases and are how the release path is exercised before a final version. |

## 8. Quality requirements

| Requirement | Detail |
| --- | --- |
| Unit tests | Each operation's file-system behaviour, atomicity, error-to-exit-code mapping, and the log records emitted for every outcome, asserted through an in-memory logger. |
| Integration tests | The real executable as a child process: argument parsing, exit codes, stdout/stderr discipline, JSON log schema, and that logged facts match reality; multi-megabyte binary round-trips verified byte for byte. |
| Two compilers, sanitized | The full suite runs under GCC 14 and Clang 18 with AddressSanitizer and UndefinedBehaviorSanitizer (`-fno-sanitize-recover=all`), and again in the release configuration whose binary is packaged. |
| Artefact tests | A smoke script exercises the installed package on four distributions. |
| Permission tests | Run as an unprivileged user in CI (root bypasses file modes). |
| Static checks | Compiler warnings as errors (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion …`); clang-tidy with every enabled check as an error; clang-format; shellcheck, shfmt, hadolint, yamllint, markdownlint, editorconfig; workflow linting (actionlint, zizmor); CodeQL; secret scanning in hooks and CI. |
| Pipeline | Every pipeline job is a required status on `main`; merges only through pull requests with linear history; the pipeline is reproducible locally with `make` and `act`. |
| Supply chain | Actions pinned to commit SHAs; a committed Conan lockfile used strictly; compiler-pinned Conan profiles; a pinned toolchain image; a seven-day cooldown on dependency updates; least-privilege permissions on every job. |

## 9. Acceptance criteria

The product is acceptable when all of the following hold, and each is enforced by an automated
check that fails the build if it stops holding:

1. `create`, `copy`, `combine` and `delete` behave as specified in section 5 on Debian 12.
2. No command writes to stdout other than `--help` and `--version`.
3. Every failure mode maps to the documented exit code, logged and returned identically.
4. No operation leaves a partially written destination, under any error path the tests can induce.
5. The test suite passes under both compilers with sanitizers enabled.
6. The released `.deb` installs, runs and removes cleanly on all four supported distributions.
7. Every release asset verifies by checksum and by build provenance against the release workflow.
8. `make docker-test` on a developer machine and the CI `Test` jobs run the same commands against
   the same toolchain and produce the same result.

## 10. Open questions and future work

| Item | Position |
| --- | --- |
| arm64 package | Not requested. Needs a cross toolchain or a second builder; add when a consumer needs it. |
| Reproducible builds verified by double build | Inputs are pinned and timestamps fixed; a CI job that builds a tag twice and compares hashes would turn the intent into a proven property. |
| Conan vulnerability auditing | No Dependabot or audit for Conan; dependencies are pinned and bumped by hand. With opentelemetry-cpp, libcurl and OpenSSL in the graph this is now the most valuable gap to close (a scheduled `conan audit` job or an SBOM scan). |
| AI-assisted failure diagnosis | Implemented as an opt-in job; dormant until a repository secret is provided. |
