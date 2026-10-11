# ADR 0005: Two compilers, warnings as errors, sanitizers in Debug, a fully static release binary

**Status:** accepted · **Date:** 2026-10-10

## Context

C++ has no single reference compiler, and the language admits programs that compile cleanly yet
read freed memory or overflow integers. A pipeline for a C++ tool has to decide which compiler
ships, which others are consulted, and how undefined behaviour is turned into a red build rather
than a latent bug. It also has to decide what the shipped binary depends on at run time.

## Decision

- **GCC 14 is the release compiler.** The `gcc-release` preset (`-O2`, `-static`) produces the
  packaged binary. GCC is the system compiler of every target distribution.
- **The release binary is linked fully statically**: libstdc++, libgcc and glibc. The `.deb` declares
  no dependencies, and `scripts/package.sh` refuses to package a binary with any dynamic `NEEDED`
  entry. A lintian override, installed with the package, documents the choice.
- **Clang 18 is the second compiler**, built and tested in CI with the same flags, and the source of
  `clang-tidy` (every enabled check is an error, opt-outs documented in `.clang-tidy`) and
  `clang-format` (version 18 only; `scripts/format.sh` refuses other majors).
- **Warnings are errors** for the project's own targets through the `fileops::warnings` interface
  target: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast
  -Wcast-align -Wunused -Woverloaded-virtual -Wnon-virtual-dtor -Wdouble-promotion -Wformat=2
  -Wimplicit-fallthrough` (plus GCC extras) and `-Werror`. Third-party headers are system includes
  and exempt. `-Wnull-dereference` is left out on purpose: at `-O2` GCC reports it inside libstdc++'s
  own stream iterators, and UBSan plus the clang-analyzer checks cover the real cases.
- **Every Debug preset enables AddressSanitizer and UndefinedBehaviorSanitizer** with
  `-fno-sanitize-recover=all`, so any finding aborts the test. Sanitizers and the static link are
  mutually exclusive (the sanitizer runtimes are shared libraries) and CMake refuses the combination.
- **C++20** is the language standard (`std::format`, `std::filesystem`, designated initialisers).

## Reasons

- Two front ends disagree usefully: each accepts code the other diagnoses. Running both costs one
  more container job, not more code.
- Sanitizers find the bug class static analysis cannot; running the whole suite under them on every
  push is the cheapest memory-safety evidence available. Debug-only keeps the shipped binary free of
  instrumentation.
- Static linking was first tried for libstdc++ and libgcc only, leaving glibc dynamic as Debian
  policy prefers. That binary did not run on Debian 12 or Ubuntu 22.04: the toolchain image is
  Ubuntu 24.04 (glibc 2.39) and its libstdc++ calls glibc 2.36 and 2.38 symbols (`arc4random` for
  `std::random_device`, `__isoc23_strtoll` behind `std::stoll`), so the package would have needed
  `libc6 (>= 2.38)`. The alternatives were an older build image (no GCC 14 or `std::format` in the
  Ubuntu 22.04 or Debian 12 archives), a compiler from a third-party PPA (unpinned supply chain), or
  a musl toolchain (a second libc and a non-Debian image without lintian). Static glibc costs none
  of that.
- The documented hazards of static glibc are name-service, locale and `dlopen` lookups, which this
  tool never performs; the linker emits no such warnings for it. The binary is about 2.5 MB.

## Consequences

- The toolchain image must ship both compilers at the pinned majors; the Conan profiles and presets
  name them explicitly.
- Dependency binaries (GoogleTest) are built per compiler by Conan, cached per lockfile and profile.
- clang-tidy needs a compile database, so it runs on the `clang-debug` leg in CI after that
  configure; locally `make tidy` configures the current preset first.
- A warning introduced by a compiler upgrade breaks the build on purpose; the fix is a code change
  or a documented opt-out, not a relaxed flag.
- macOS development uses Apple Clang through the `dev` preset with the same warnings and sanitizers,
  but it is not a release or CI compiler.
- A future glibc feature the tool starts to need (NSS, locales) would reopen the linking decision;
  the install smoke test on all four distributions is what would catch a regression.
