# Observability contract

`fileops` treats its own telemetry as part of the interface: tests assert on it, CI publishes it,
and a failure can be diagnosed from the log stream alone. This document is the contract. The event
ids, names, message templates and JSON shape are identical to the C# `fileops` tool's, so one log
consumer serves both.

## Channels

| Channel | Where | Enabled by |
| --- | --- | --- |
| Logs | stderr, text or NDJSON | `--log-level` / `FILEOPS_LOG_LEVEL` (default `None`), `--log-format` / `FILEOPS_LOG_FORMAT` (default `text`), `-v` for Information |
| Logs, traces, metrics | OTLP/HTTP (protobuf) | `OTEL_EXPORTER_OTLP_ENDPOINT` (standard `OTEL_*` variables honoured); log records at Information and above by default while the console stays silent, `--log-level` governs every sink |
| User-facing result | stderr, last line, `fileops: <message>` | always, on failure only |

**stdout is never used** except by `--help` and `--version`, so the tool is safe in pipelines. A usage
error prints one `fileops: <message>` line per error plus `fileops: run 'fileops --help' for usage` on
stderr and nothing on stdout. Sinks write synchronously and the OTLP exporters are flushed before
the summary line is written, so telemetry always precedes the summary line and the process exit.

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
  `CollectingLogger` and a `CollectingObserver` (`tests/support/`) attached and assert on the
  captured records — ids, levels, order, fields — and on the span/metric facts (context, exit code,
  bytes, duration, error type) alongside the file-system outcome. `ObservedOperations` is the
  harness; `field<T>(record, name)` reads a typed field. `otel_mapping_test.cpp` drives the real
  OpenTelemetry mapping with in-memory span and metric exporters and checks names, tags, status and
  instruments against the tables above.
- **Integration tests** (`tests/integration`) launch the real executable through the `Tool` harness
  with `FILEOPS_LOG_FORMAT=json` and `FILEOPS_LOG_LEVEL=Debug`, parse stderr line by line, and check
  that the logged exit code, byte counts and event sequence match what the process actually returned
  and wrote. With `FILEOPS_TEST_LOG_DIR` set, every invocation is appended to `cli-runs.ndjson` as a
  header `{"run":{"args":[...],"exitCode":N,"binary":"..."}}` followed by its records.
  `otlp_export_test.cpp` starts an in-process OTLP/HTTP receiver, points the binary at it and
  checks that spans, metrics and log records arrive as protobuf, that the default export level is
  Information, and that `--log-level None` silences the export too.
- **CI** sets `FILEOPS_TEST_LOG_DIR=artifacts/logs/<preset>` (Makefile `test` target), renders the
  file into the job summary (`scripts/telemetry-summary.sh`) and uploads it as an artefact. On
  failure, the optional `diagnose` job hands the same files to an AI assistant with the event-id
  table above, so the first read of a red build already names the failing event and the likely cause.

## Traces

One span per operation from a tracer named `fileops` (instrumentation scope version = the tool
version), named `fileops.<operation>`. The core reports the facts through `telemetry::Observer`
(`include/fileops/telemetry/observer.hpp`); `src/cli/otel.cpp` turns them into the span.

| Tag | Meaning |
| --- | --- |
| `fileops.operation` | `create` / `copy` / `combine` / `delete` |
| `file.path` | the file produced or removed |
| `file.source`, `file.source2`, `file.destination` | inputs and output for copy/combine |
| `file.size` | bytes written or removed |
| `fileops.overwrite` | whether `--force` was given (create, copy, combine) |
| `fileops.exit_code` | numeric exit code (0 on success) |
| `error.type` | `fileops::Error`, `std::filesystem::filesystem_error` or `std::exception` on failure |

Status is `Ok` on success, `Error` with the user-facing message as description on failure. The span
is the active one while the operation runs, so exported log records carry its trace and span ids.

## Metrics

Meter `fileops`:

| Instrument | Type | Unit | Tags |
| --- | --- | --- | --- |
| `fileops.operations` | counter | `{operation}` | `operation`, `outcome` (`success` or the lower-case exit code name, e.g. `notfound`) |
| `fileops.operation.duration` | histogram | `s` | same |
| `fileops.bytes` | counter | `By` | same; only recorded when bytes > 0 |

## Shipping telemetry somewhere

```sh
docker run -d -p 4318:4318 otel/opentelemetry-collector     # or any OTLP/HTTP endpoint
OTEL_EXPORTER_OTLP_ENDPOINT=http://localhost:4318 fileops copy a b
```

Only OTLP over HTTP/protobuf is built in (`/v1/traces`, `/v1/metrics`, `/v1/logs` under the
endpoint); `OTEL_EXPORTER_OTLP_HEADERS`, `OTEL_EXPORTER_OTLP_TIMEOUT`, `OTEL_RESOURCE_ATTRIBUTES` and
`OTEL_SERVICE_NAME` are read by the SDK. Resource attributes: `service.name=fileops`,
`service.version=<version>`. An unreachable endpoint costs one timeout (10 s by default) and never
changes the exit code.
