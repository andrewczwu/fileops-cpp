# ADR 0001: C++20 with CMake, Ninja and Conan 2

**Status:** accepted · **Date:** 2026-10-10

## Context

The deliverable is a small file tool that must ship as a single portable executable installable on
Debian, with the evaluation focused on CI/CD practice, version control and implementation quality.
This repository builds it with the toolchain the role itself uses — C++ and CMake — so the
pipeline practices are demonstrated where they are hardest: compiler pinning, dependency
management without a central registry, memory safety, and a binary that must run on older glibc.

## Decision

C++20, built with CMake 3.25+ and Ninja, described by `CMakePresets.json`, with dependencies
resolved by Conan 2 from a committed lockfile. Third-party code is limited to three packages:
CLI11 (argument parsing), nlohmann_json (the JSON log sink), opentelemetry-cpp with its OTLP/HTTP
exporter (traces, metrics and log export; brings protobuf, libcurl, OpenSSL) and GoogleTest (tests
only). The core
library uses POSIX calls and the standard library alone.

## Reasons

- CMake with presets gives one command per configuration (`cmake --preset gcc-release`) that is
  identical on a laptop, in the container and in CI; Ninja makes incremental builds fast.
- Conan 2 is the mainstream C++ package manager with a public recipe index, lockfiles, and profiles
  that pin the compiler so dependency binaries match the project's; it generates plain CMake
  `find_package` configs, so the CMake side stays conventional.
- A fully static link yields a binary with no run-time dependencies at all, satisfying "single
  portable executable" literally (see ADR 0005 for why glibc is included in that).
- GoogleTest is the de-facto C++ test framework and integrates with ctest discovery and JUnit output.
- CLI11 and nlohmann_json are header-only, widely used and small; they keep the CLI and the JSON
  sink short. opentelemetry-cpp is the reference SDK; OTLP over HTTP only, since gRPC would add a
  large toolchain for no gain, and libcurl resolves names with c-ares so DNS works inside the static
  binary (static glibc cannot load NSS modules).

## Consequences

- Compilers are a pinned input, not an assumption: the Conan profiles name `gcc-14` / `clang-18`,
  the presets pass the same compilers to CMake, and the toolchain image installs exactly those.
- `conan.lock` must be regenerated and committed when a dependency changes; CI resolves strictly
  against it. Dependabot has no Conan ecosystem, so bumps are manual (see CONTRIBUTING.md).
- The Linux binary and the `.deb` are built in the Docker toolchain image; macOS is a development
  loop (`dev` preset with Apple Clang) rather than a release target.
- `std::chrono` time-zone support is absent from Apple's libc++, so the text log sink prints UTC
  everywhere rather than local time on some platforms and UTC on others.
- GoogleTest is built from source once per lockfile change; CI caches Conan's package folder.
