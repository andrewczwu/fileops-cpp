# ADR 0002: Atomic writes and refuse-to-overwrite by default

**Status:** accepted · **Date:** 2026-10-10

## Context

`create`, `copy` and `combine` all produce a file. A naive implementation opens the destination and
streams into it, which leaves a truncated file if the process dies or an input turns out to be
unreadable half way through, and silently clobbers existing data. C++ gives direct access to the
POSIX primitives that make the alternative exact.

## Decision

1. All writes go through `atomic_file::write`: content goes to `.<name>.<16 hex>.tmp` in the
   destination directory, created with `open(O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC, mode)` where `mode`
   is the final permission bits (the source's for `copy`, the existing file's for `--force`), then
   `fchmod` to the exact bits, `fsync`, and a single commit step: `rename(2)` when overwriting,
   `link(2)` + `unlink` when not.
2. An existing destination is an error (exit code 3) unless `--force` is given.
3. `delete` refuses directories and, with `--force`, tolerates a missing file (`rm -f` semantics).
4. The temp name's `<name>` part is truncated on a UTF-8 character boundary so the decorated name
   stays within the 255-byte limit; a RAII guard removes the temp file on any early exit.

## Reasons

- `rename(2)` is atomic within a file system; readers see either the old file or the complete new
  one, never a partial write. Placing the temp file beside the destination guarantees the same
  file system.
- `link(2)` fails with `EEXIST` if a destination appears between the up-front check and the commit,
  closing the race without a lock; file systems that refuse hard links fall back to a stat-then-rename.
- Passing the mode to `open` means the file never exists with wider permissions than intended, not
  even between creation and `fchmod` (a test observes the mode from inside the writer callback).
- Because inputs are fully consumed before the commit, `combine a b a --force` is a safe in-place
  append, a property that falls out of the design rather than needing special code.
- Safe-by-default matches the expectations of a tool that may be driven by scripts; destructive
  behaviour must be requested explicitly.

## Consequences

- Each write briefly needs space for a second copy of the file.
- The destination directory must be writable even when only overwriting a file.
- A process killed by a signal can leave a `.name.<id>.tmp` sibling; the destination is untouched.
- Tests check for leftover `.*.tmp` files after every failure path, and that a writer that throws
  leaves no destination (or the original content, when overwriting).
