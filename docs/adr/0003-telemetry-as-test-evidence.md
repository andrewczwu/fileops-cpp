# ADR 0003: Telemetry is part of the contract and tests assert on it

**Status:** accepted · **Date:** 2026-10-10

## Context

Logs are usually added for operators and ignored by tests. That leaves two gaps: the logs drift
from reality without anyone noticing, and when CI fails the first step is always "re-run with more
logging". Both are expensive in a pipeline whose job is developer velocity.

## Decision

- Every operation emits a fixed set of structured log events with stable ids and names, documented
  in `docs/OBSERVABILITY.md`, identical to the C# tool's so the schema is shared.
- The core library logs only through an abstract `telemetry::Logger` (`enabled(level)`, `log(record)`);
  sinks live outside it: `TextSink` and `JsonSink` in the CLI, `CollectingLogger` in the tests.
- Unit tests capture records with `CollectingLogger` and assert on ids, levels, order and typed
  fields next to the file-system assertions.
- Integration tests run the real binary with JSON logging and verify that the logged exit code,
  byte counts and event order agree with what the process returned and what is on disk.
- CI captures every invocation's log stream to `cli-runs.ndjson`, renders a summary into the job
  summary, uploads it, and (optionally) feeds it to an AI assistant for failure diagnosis.
- Traces and metrics are deferred to a second phase; the `Logger` seam and `execute()` are where they
  will attach.

## Reasons

- A log line that a test depends on cannot silently rot.
- A red build already contains the evidence needed to explain it; no re-run is required.
- Machine-readable, schema-stable output is what makes automated or AI-assisted triage reliable:
  the assistant is given event ids and field names, not free text to guess at.
- Keeping the sink interface tiny means the core library has no logging dependency at all, which is
  what lets it build with nothing but the standard library.

## Consequences

- Changing a message template, event id or field name is a breaking change for tests (and for the
  C# tool's consumers) and must be deliberate.
- Logging must never touch stdout; both sinks are constructed over `std::cerr` and the integration
  tests check stdout is empty on every path.
- Without spans there is no trace/span id to correlate records; phase 2 adds that.
