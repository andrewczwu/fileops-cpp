#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>

#include <sys/types.h>

namespace fileops::atomic_file {

/// Receives the open, write-only descriptor of the temporary file and writes the content.
using Writer = std::function<void(int fd)>;

/// Writes `destination` via a temporary sibling file so that a crash or error never leaves a
/// partially written destination behind. Content goes to `.<name>.<random>.tmp` in the destination
/// directory, is flushed to disk, and is then linked or renamed into place: a single atomic step on
/// POSIX file systems.
///
/// `mode` is passed to the open(2) call that creates the temp file, so there is no instant at which
/// it exists with wider permissions than intended. When nullopt, an overwritten destination keeps
/// its existing bits and a new file gets the process default.
///
/// Returns the number of bytes written. Throws Error with ExitCode::NotFound if the destination
/// directory does not exist, ExitCode::AlreadyExists if the destination exists and `overwrite` is
/// false, ExitCode::IoError for anything the file system refuses.
std::uint64_t write(const std::filesystem::path& destination,
                    bool overwrite,
                    const Writer& writer,
                    std::optional<mode_t> mode = std::nullopt);

/// Writes all of `size` bytes from `data` to `fd`, retrying on short writes and EINTR.
void write_all(int fd, const void* data, std::size_t size);

}  // namespace fileops::atomic_file
