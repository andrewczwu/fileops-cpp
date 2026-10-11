# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org). The release workflow refuses to publish a tag that
has no matching section below.

## [Unreleased]

## [1.0.0] - 2026-10-10

### Added

- `create`, `copy`, `combine` and `delete` commands with safe-by-default overwrite handling
  (`--force`), the same command-line contract as the C# `fileops` tool.
- Atomic writes: output goes to a temporary sibling file created with the final permission bits,
  flushed, and renamed (or hard-linked when not overwriting) into place, so an interrupted run never
  leaves a partial file. `combine` can therefore append in place (`combine a b a --force`).
  Names up to the 255-byte file-system limit work; the temporary name is shortened on a UTF-8
  character boundary.
- `delete` removes symbolic links themselves (dangling ones and links to directories included)
  rather than looking through them; writes flush the destination directory after the rename so
  the new name is durable, not just its content.
- Stable exit codes: 0 success, 1 usage, 2 not found, 3 already exists, 4 I/O error, 70 internal.
- OpenTelemetry: one span per operation, an operations counter, a duration histogram and a bytes
  counter, exported over OTLP/HTTP together with log records when `OTEL_EXPORTER_OTLP_ENDPOINT` is
  set; the same span tags and instruments as the C# tool. Verified end to end by an integration
  test that receives the OTLP payloads.
- Structured logging (`--log-level`, `--log-format text|json`, `FILEOPS_LOG_*` env) to stderr with
  stable event ids and names; stdout is reserved for results. Usage errors are `fileops: <message>`
  lines on stderr with nothing on stdout.
- Single native executable: C++20, GCC 14, `-O2`, linked fully statically so the `.deb` has no
  dependencies and runs on any x86-64 Linux; man page, DEP-5 copyright, changelog and a documented
  lintian override for the static binary included; lintian-clean at warning level.
- Dependencies through Conan 2 with a committed `conan.lock`, compiler-pinned profiles and CMake
  presets for every configuration.
- Tests: 59 unit tests with in-memory log and span/metric collectors and 40 integration tests against the real
  binary, run under GCC 14 and Clang 18 with AddressSanitizer and UndefinedBehaviorSanitizer, and
  again in the release configuration that is packaged.
- CI: clang-format, secret scanning, shell/Docker/YAML/Markdown/workflow linting, clang-tidy, the
  sanitized test matrix, release build with lintian, telemetry evidence in the job summary, and
  install smoke tests on Debian 12/13 and Ubuntu 22.04/24.04.
- Release workflow: tag-driven, with SLSA build provenance attestations and checksums; a pinned
  toolchain image published to GHCR and rebuilt weekly.

[Unreleased]: https://github.com/andrewczwu/fileops-cpp/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/andrewczwu/fileops-cpp/releases/tag/v1.0.0
