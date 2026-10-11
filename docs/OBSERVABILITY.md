# Observability contract

`fileops` treats its own telemetry as part of the interface: tests assert on it, CI publishes it,
and a failure can be diagnosed from the log stream alone. This document is the contract. The event
ids, names, message templates and JSON shape are identical to the C# `fileops` tool's, so one log
consumer serves both.

## Channels

| Channel | Where | Enabled by |
| --- | --- | --- |
| Logs | stderr, text or NDJSON | `--log-level` / `FILEOPS_LOG_LEVEL` (default `None`), `--log-format` / `FILEOPS_LOG_FORMAT` (default `text`), `-v` for Information |
| User-facing result | stderr, last line, `fileops: <message>` | always, on failure only |

**stdout is never used** except by `--help` and `--version`, so the tool is safe in pipelines. A usage
error prints one `fileops: <message>` line per error plus `fileops: run 'fileops --help' for usage` on
stderr and nothing on stdout. Sinks write synchronously, so log records always precede the summary
line and the process exit.

Traces and metrics are not implemented in this version; see "Phase 2" below.

## Log events

Every record carries a stable `EventId` and `EventName`. Ranges: 1xxx lifecycle, 2xxx create, 3xxx copy,
4xxx combine, 5xxx delete. Defined in `include/fileops/telemetry/log_events.hpp`; the message templates
and field sets live in `src/core/log.cpp`, one function per event.

| Id | Name | Level | Message template | Structured fields |
| ---: | --- | --- | --- | --- |
| 1000 | OperationStarted | Debug | `Starting {Operation} on {Path}` | Operation, Path |
| 1001 | OperationCompleted | Information | `{Operation} completed on {Path}: {Bytes} bytes in {DurationMs} ms` | Operation, Path, Bytes, DurationMs |
| 1002 | OperationFailed | Error | `{Operation} failed on {Path} with exit code {ExitCode}: {Reason}` | Operation, Path, ExitCode, Reason |
| 2001 | FileCreated | Information | `Created {Path} ({Bytes} bytes)` | Path, Bytes |
| 3001 | FileCopied | Information | `Copied {Source} to {Destination} ({Bytes} bytes)` | Source, Destination, Bytes |
| 4001 | FilesCombined | Information | `Combined {First} and {Second} into {Output} ({Bytes} bytes)` | First, Second, Output, Bytes |
| 5001 | FileDeleted | Information | `Deleted {Path} ({Bytes} bytes)` | Path, Bytes |
| 5002 | DeleteSkippedMissing | Warning | `Nothing to delete: {Path} does not exist` | Path |

Field types: `Operation` is the enumerator name (`Create`, `Copy`, `Combine`, `Delete`); paths are
strings as given on the command line; `Bytes` is an integer; `DurationMs` a floating-point number;
`ExitCode` is the `ExitCode` enumerator name (`UsageError`, `NotFound`, `AlreadyExists`, `IoError`,
`InternalError`); `Reason` is the user-facing summary line without its `fileops:` prefix and the following space.
`Category` is always `fileops.core.FileOperations`.

Invariants (enforced by `tests/unit/telemetry_contract_test.cpp` and
`tests/integration/cli_contract_test.cpp`):

- a successful operation emits `1000` first and `1001` last, with exactly one operation-specific
  event between them, and nothing at Warning or above, except `5002` for `delete --force` on a
  missing path;
- a failed operation emits `1000` then `1002`, never `1001`; the logged `ExitCode` names the code the
  process returns, and the summary line on stderr is `fileops:`, a space, then `Reason`;
- an empty or whitespace path fails with `UsageError` through the same path, so it has failure telemetry;
- a usage error caught by the argument parser runs no operation and emits no records;
- with `--log-level None` (the default) nothing is written to stderr on success;
- the `--log-format json` record shape is
  `{Timestamp, EventId, EventName, LogLevel, Category, Message, State{...}}`, one object per line,
  where `Timestamp` is ISO 8601 in UTC with microseconds (`2026-10-11T03:15:38.500929Z`) and
  `LogLevel` is the level name (`Trace`, `Debug`, `Information`, `Warning`, `Error`, `Critical`).

Text format: `HH:MM:SS.mmm level: fileops.core.FileOperations[id] message`, with the four-letter
level tags `trce dbug info warn fail crit` and UTC times.

## How tests use this

- **Unit tests** (`tests/unit`) run each operation through `FileOperations` with a
  `CollectingLogger` (`tests/support/collecting_logger.hpp`) attached and assert on the captured
  records — ids, levels, order, fields — alongside the file-system outcome. `ObservedOperations`
  is the harness; `field<T>(record, name)` reads a typed field.
- **Integration tests** (`tests/integration`) launch the real executable through the `Tool` harness
  with `FILEOPS_LOG_FORMAT=json` and `FILEOPS_LOG_LEVEL=Debug`, parse stderr line by line, and check
  that the logged exit code, byte counts and event sequence match what the process actually returned
  and wrote. With `FILEOPS_TEST_LOG_DIR` set, every invocation is appended to `cli-runs.ndjson` as a
  header `{"run":{"args":[...],"exitCode":N,"binary":"..."}}` followed by its records.
- **CI** sets `FILEOPS_TEST_LOG_DIR=artifacts/logs/<preset>` (Makefile `test` target), renders the
  file into the job summary (`scripts/telemetry-summary.sh`) and uploads it as an artefact. On
  failure, the optional `diagnose` job hands the same files to an AI assistant with the event-id
  table above, so the first read of a red build already names the failing event and the likely cause.

## Phase 2: traces and metrics (not yet implemented)

The core library logs through an abstract `telemetry::Logger`, and `FileOperations::execute()` is
the single place where an operation starts, ends and is timed. The planned second phase adds
`opentelemetry-cpp` behind that seam without changing the log contract:

- one span per operation from a tracer named `fileops`, called `fileops.<operation>`, with tags
  `fileops.operation`, `file.path`, `file.source` / `file.source2` / `file.destination`, `file.size`,
  `fileops.overwrite`, `fileops.exit_code` and `error.type`; status `Ok` on success, `Error` with
  the user-facing message on failure;
- three instruments on a meter named `fileops`: `fileops.operations` (counter), `fileops.operation.duration`
  (histogram, seconds) and `fileops.bytes` (counter), tagged by `operation` and `outcome`;
- export over OTLP when `OTEL_EXPORTER_OTLP_ENDPOINT` is set, with log records at Information and
  above forwarded to the same endpoint while the console stays silent unless `--log-level` says otherwise;
- unit tests capturing spans and measurements in memory, mirroring what the C# tool's tests do today.

Until then, `OTEL_*` variables are ignored.
