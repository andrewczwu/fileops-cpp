# Architecture

A small tool, structured so that it could grow: the file logic, the command-line surface, the
telemetry and the delivery pipeline are separate concerns with explicit contracts between them.

```text
                      ┌──────────────────────────────┐
 argv ──► src/cli     │ CLI11 parse (main.cpp)       │
                      │ GlobalOptions (log level/fmt)│
                      │ make_sink: TextSink/JsonSink │
                      │ run(): Error → exit code +   │──► exit code + "fileops: …" on stderr
                      │   "fileops: msg"             │
                      └──────────────┬───────────────┘
                                     │ FileOperations::create/copy/combine/remove
                      ┌──────────────▼───────────────┐
                      │ fileops::core (libfileops_core)
                      │  FileOperations  (execute(): │──► log records via telemetry::Logger
                      │    timing, error mapping)    │
                      │  atomic_file     (tmp+rename)│──► files on disk
                      │  telemetry/*     (contract)  │
                      └──────────────────────────────┘
```

## fileops::core

Public headers live under `include/fileops/`; the library has no third-party dependencies.

- **`FileOperations`** (`operations.hpp`, `src/core/operations.cpp`) is the only entry point. Each
  operation validates its inputs (`require_path`, `reject_directory`, `require_file`, same-inode
  check via `stat`), then does its work inside `execute()`, which owns the cross-cutting behaviour:
  log `OperationStarted`, time the call with `steady_clock`, log `OperationCompleted` or
  `OperationFailed`, and translate `std::filesystem_error` and anything unexpected into `Error`
  with the right `ExitCode`. Adding a fifth operation means one method and one `Operation` enumerator.
- **`atomic_file::write`** (`atomic_file.hpp`, `src/core/atomic_file.cpp`) creates
  `.<name>.<16 hex>.tmp` in the destination directory with `open(O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC, mode)`,
  so the file never exists with wider permissions than intended, then `fchmod`s to the exact bits
  (the umask only applies at `open`), hands the descriptor to the caller's writer, `fsync`s, and
  commits: `rename(2)` when overwriting, otherwise `link(2)` + `unlink` of the temp name, which
  fails with `EEXIST` if the destination appeared in the meantime (a rename-after-stat fallback
  covers file systems without hard links). A RAII guard removes the temp file on any early exit.
  The name component is truncated on a UTF-8 boundary so the decorated temp name fits in 255 bytes.
  `copy` passes the source's mode; `create`/`combine --force` reuse the existing file's bits.
- **`Error`** (`error.hpp`) is a `std::runtime_error` carrying an `ExitCode` and the path; its
  message is safe to print verbatim. The CLI never inspects exception types beyond this one.
- **`telemetry/`** holds the contract: `log_events.hpp` (ids and names), `log.hpp` (`LogLevel`,
  `LogRecord`, the abstract `Logger`, `NullLogger`) and `observer.hpp` (`OperationContext`,
  `OperationOutcome`, the abstract `Observer`/`OperationScope`, `NullObserver`): the span and metric
  facts, reported by `execute()` with no OpenTelemetry dependency in the core.
  `src/core/log.hpp/.cpp` has one function per event that builds the record with a fixed message
  template and field set. See
  [OBSERVABILITY.md](OBSERVABILITY.md).

## src/cli

- **`main.cpp`** builds the CLI11 app: four subcommands, each a thin lambda from parsed arguments
  to one `FileOperations` call; global options `--log-level` (env `FILEOPS_LOG_LEVEL`),
  `--log-format` (env `FILEOPS_LOG_FORMAT`), `-v/--verbose`, `--version` (from the
  `FILEOPS_VERSION_STRING` compile definition). `fallthrough(true)` lets global options follow the
  subcommand. `CLI::ParseError` becomes `fileops: <message>` lines on stderr plus the usage hint,
  exit 1, nothing on stdout; `--help` and `--version` are the only writers to stdout.
- **`sinks.cpp`** provides `TextSink` (`HH:MM:SS.mmm level: category[id] message`, UTC) and
  `JsonSink` (one `nlohmann::ordered_json` object per line) over `std::ostream`; `make_sink` returns
  a `NullLogger` when the level is `None`. Both write to `std::cerr` synchronously, so records
  always precede the summary line.
- **`otel.cpp`** (library `fileops::otel`, shared with the unit tests) implements the core's
  `telemetry::Observer` with opentelemetry-cpp: one span `fileops.<operation>` with the documented
  tags, the three instruments, and an OTLP log sink that forwards `LogRecord`s to the logs API.
  `Runtime::start_if_configured` builds the SDK providers with OTLP/HTTP exporters only when
  `OTEL_EXPORTER_OTLP_ENDPOINT` is set; the exporters read the other `OTEL_*` variables themselves.
- **`run()`** starts the OpenTelemetry runtime if configured, tees the console sink and the OTLP
  log sink (the latter at Information unless `--log-level` was given), executes the chosen action,
  flushes and shuts the runtime down, and only then prints the summary line and maps the outcome:
  `Error` → its exit code and message, any other exception → 70.

## Tests

| Suite | Runs against | Proves |
| --- | --- | --- |
| `tests/unit` (59) | `libfileops_core` in-process with `CollectingLogger` | behaviour of each operation, atomicity, error mapping, and the log records emitted for every outcome |
| `tests/integration` (40) | the real executable (`$<TARGET_FILE:fileops>` by default, any binary via `FILEOPS_BIN`), spawned with `fork`/`execve` and both pipes drained with `poll` | argument parsing, exit codes, stdout/stderr discipline, JSON log schema, and that logged facts match reality |

`tests/support/` has the shared harness: `TempDirectory` (`mkdtemp`, leftover temp-file detection),
`CollectingLogger`, `FILEOPS_SKIP_IF_ROOT` for permission tests. `gtest_discover_tests` registers
each case with ctest (`unit.` / `integration.` prefixes), and `ctest --output-junit` produces the
report CI publishes. Both suites run in every preset, so the release binary is exercised by the same
integration tests as the sanitized Debug builds; a smoke script then tests the installed `.deb`.

## Delivery

```text
docker/Dockerfile.build ─► ghcr.io/andrewczwu/fileops-cpp-build   (weekly + on change)
                                      │
   PR / push ─► ci.yml:  format · secrets · lint · test×2 (gcc-debug, clang-debug + tidy)
                         · build (gcc-release: test → CPack .deb → lintian) ─► smoke×4
                                      │
   tag vX.Y.Z ─► release.yml: build ─► verify×4 ─► attest + GitHub release (.deb, .tar.gz, SHA256SUMS)
```

Every multi-step CI task is a script under `scripts/`, usually invoked through the `Makefile`, so
`make test`, `make package`, `make smoke` locally are the same commands the pipeline runs, and
`act` can execute the workflows locally against the same toolchain image.

### Pipeline shape

Pulling the toolchain image (two compilers, CMake, Conan, lintian) costs roughly 25 s per container
job on a hosted runner, more than most individual steps. The job graph therefore has exactly as many
container jobs as there are configurations worth building, and no more:

| Job | Preset | Why it exists |
| --- | --- | --- |
| `Test (gcc-debug)` | `gcc-debug` | the release compiler under ASan/UBSan |
| `Test (clang-debug)` | `clang-debug` | a second front end catches what GCC accepts silently; it also produces the compile database clang-tidy needs, so tidy runs here |
| `Build, test & package (gcc-release)` | `gcc-release` | the shipped configuration: build, run the same tests, CPack, lintian — one job, one pull |

The three run in parallel; `smoke` waits for `build` only. clang-format and the other linters need
no toolchain and run on the host. Within each container job, Conan's package cache (`$CONAN_HOME/p`)
is restored from `actions/cache` keyed on `conan.lock` and the profiles, so GoogleTest is compiled
once per lockfile change rather than per run.

### Reproducible dependency resolution

Conan is a package manager for C and C++: `conanfile.py` names the dependencies, and `conan install`
downloads or builds them and generates two things for CMake — a toolchain file (compiler, standard,
flags from the profile) and `find_package` config files for each dependency. The pieces that keep
this deterministic:

- `conan.lock` pins every resolved recipe revision; the Makefile and CI always pass `--lockfile conan.lock`,
  so a floating version cannot sneak into a build.
- `conan/profiles/*` pin the compiler, its version, the C++ standard and the standard library
  (`linux-gcc14`, `linux-clang18`, `macos-apple-clang`); dependency binaries are built with the same
  compiler that links the project. Each profile also declares `[platform_tool_requires] cmake/<version>`,
  which tells Conan the system CMake satisfies GoogleTest's build requirement instead of downloading
  another CMake.
- `CMakePresets.json` maps each preset to `build/<preset>/` and expects the toolchain at
  `build/<preset>/generators/conan_toolchain.cmake` (the `layout()` in `conanfile.py` puts it there).
  `make deps` runs the install, `make configure` the preset; the Makefile derives the profile and
  build type from the preset name.
- The toolchain image pins Conan itself (`2.33.0` via pipx) and takes compilers from the Ubuntu 24.04
  archive by major version; the weekly rebuild picks up security updates.

### Versioning

The git tag is the single source of truth. `release.yml` derives the version from `vX.Y.Z` with
`scripts/ci/version-from-tag.sh`, refuses tags without a matching `CHANGELOG.md` section, and passes
it as `FILEOPS_VERSION`, which the presets forward to CMake as a cache variable (`$env{FILEOPS_VERSION}`;
empty means `0.0.0`). `project(VERSION)` gets the numeric part; the CLI embeds the full string;
`cmake/Packaging.cmake` maps a `-pre` suffix to Debian's `~` for the package version while keeping
the SemVer form in the file name, because GitHub rewrites `~` in release asset names. Local builds
use `0.0.0-dev`, CI builds `0.0.0-ci.N`, so both sort below any real release.
