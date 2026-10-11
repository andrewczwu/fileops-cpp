#include "fileops/operations.hpp"

#include "fileops/atomic_file.hpp"
#include "fileops/error.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <format>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.hpp"

namespace fileops {
namespace {

constexpr std::size_t copy_buffer_size = std::size_t{1024} * 1024;

/// An empty argument is a usage error (exit 1), reported through the normal failure path.
void require_path(const std::filesystem::path& value, std::string_view name) {
    const std::string text = value.string();
    const bool blank =
        std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    if (blank) {
        throw Error(ExitCode::UsageError, std::format("{} must not be empty", name), value);
    }
}

void reject_directory(const std::filesystem::path& path) {
    if (std::filesystem::is_directory(path)) {
        throw Error(ExitCode::UsageError, std::format("is a directory: {}", path.string()), path);
    }
}

void require_file(const std::filesystem::path& path) {
    reject_directory(path);
    if (!std::filesystem::exists(path)) {
        throw Error(ExitCode::NotFound, std::format("file not found: {}", path.string()), path);
    }
}

/// True when both paths name the same inode (follows symbolic links); false if either is missing.
bool same_file(const std::filesystem::path& a, const std::filesystem::path& b) {
    struct stat sa {};
    struct stat sb {};
    if (::stat(a.c_str(), &sa) != 0 || ::stat(b.c_str(), &sb) != 0) {
        return false;
    }
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

/// errno as text without strerror(3), whose static buffer is not thread safe.
std::string errno_text(int err) {
    return std::generic_category().message(err);
}

[[noreturn]] void throw_errno(int err, const std::filesystem::path& path) {
    switch (err) {
        case ENOENT:
            throw Error(ExitCode::NotFound, std::format("file not found: {}", path.string()), path);
        case EACCES:
        case EPERM:
            throw Error(ExitCode::IoError, std::format("permission denied: {}", path.string()), path);
        default:
            throw Error(
                ExitCode::IoError, std::format("i/o error on {}: {}", path.string(), errno_text(err)), path);
    }
}

class ReadFile {
public:
    explicit ReadFile(const std::filesystem::path& path) : fd_(::open(path.c_str(), O_RDONLY | O_CLOEXEC)) {
        if (fd_ < 0) {
            throw_errno(errno, path);
        }
    }
    ReadFile(const ReadFile&) = delete;
    ReadFile& operator=(const ReadFile&) = delete;
    ReadFile(ReadFile&&) = delete;
    ReadFile& operator=(ReadFile&&) = delete;
    ~ReadFile() { ::close(fd_); }

    [[nodiscard]] int fd() const noexcept { return fd_; }

    [[nodiscard]] mode_t permission_bits() const {
        struct stat st {};
        if (::fstat(fd_, &st) != 0) {
            throw Error(ExitCode::IoError, std::format("stat failed: {}", errno_text(errno)));
        }
        return st.st_mode & 07777U;
    }

    /// Streams the whole file to `out` in fixed-size chunks; memory use is bounded by the buffer.
    void stream_to(int out) const {
        std::vector<char> buffer(copy_buffer_size);
        for (;;) {
            const ssize_t n = ::read(fd_, buffer.data(), buffer.size());
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw Error(ExitCode::IoError, std::format("read failed: {}", errno_text(errno)));
            }
            if (n == 0) {
                return;
            }
            atomic_file::write_all(out, buffer.data(), static_cast<std::size_t>(n));
        }
    }

private:
    int fd_;
};

Error translate(const std::filesystem::filesystem_error& e, const std::filesystem::path& path) {
    const auto code = e.code();
    if (code == std::errc::no_such_file_or_directory) {
        return {ExitCode::NotFound, std::format("not found: {}", path.string()), path};
    }
    if (code == std::errc::permission_denied || code == std::errc::operation_not_permitted) {
        return {ExitCode::IoError, std::format("permission denied: {}", path.string()), path};
    }
    return {ExitCode::IoError, std::format("i/o error on {}: {}", path.string(), code.message()), path};
}

}  // namespace

Result FileOperations::create(const std::filesystem::path& path,
                              std::optional<std::string_view> text,
                              bool overwrite) {
    return execute(Operation::Create, path, [&] {
        require_path(path, "path");
        reject_directory(path);

        const std::uint64_t bytes = atomic_file::write(path, overwrite, [&](int fd) {
            if (text && !text->empty()) {
                atomic_file::write_all(fd, text->data(), text->size());
            }
        });

        core_log::file_created(logger_, path, bytes);
        return Result{Operation::Create, path, bytes};
    });
}

Result FileOperations::copy(const std::filesystem::path& source,
                            const std::filesystem::path& destination,
                            bool overwrite) {
    return execute(Operation::Copy, destination, [&] {
        require_path(source, "source");
        require_path(destination, "destination");
        require_file(source);
        reject_directory(destination);
        if (same_file(source, destination)) {
            throw Error(ExitCode::UsageError,
                        std::format("source and destination are the same file: {}", source.string()),
                        source);
        }

        // The temp file is created with the source's permission bits, so the copy is never readable
        // more widely than the original, not even for the instant before the rename.
        const ReadFile input(source);
        const mode_t mode = input.permission_bits();
        const std::uint64_t bytes =
            atomic_file::write(destination, overwrite, [&](int fd) { input.stream_to(fd); }, mode);

        core_log::file_copied(logger_, source, destination, bytes);
        return Result{Operation::Copy, destination, bytes};
    });
}

Result FileOperations::combine(const std::filesystem::path& first,
                               const std::filesystem::path& second,
                               const std::filesystem::path& output,
                               bool overwrite) {
    return execute(Operation::Combine, output, [&] {
        require_path(first, "first");
        require_path(second, "second");
        require_path(output, "output");
        require_file(first);
        require_file(second);
        reject_directory(output);

        const std::uint64_t bytes = atomic_file::write(output, overwrite, [&](int fd) {
            ReadFile(first).stream_to(fd);
            ReadFile(second).stream_to(fd);
        });

        core_log::files_combined(logger_, first, second, output, bytes);
        return Result{Operation::Combine, output, bytes};
    });
}

Result FileOperations::remove(const std::filesystem::path& path, bool ignore_missing) {
    return execute(Operation::Delete, path, [&] {
        require_path(path, "path");
        // The link itself is what gets removed, so look at it rather than through it: a symlink to a
        // directory is deletable, and a dangling symlink exists.
        if (std::filesystem::is_directory(std::filesystem::symlink_status(path))) {
            throw Error(ExitCode::UsageError, std::format("is a directory: {}", path.string()), path);
        }

        struct stat st {};
        if (::lstat(path.c_str(), &st) != 0) {
            if (errno != ENOENT) {
                throw_errno(errno, path);
            }
            if (!ignore_missing) {
                throw Error(ExitCode::NotFound, std::format("file not found: {}", path.string()), path);
            }
            core_log::delete_skipped_missing(logger_, path);
            return Result{Operation::Delete, path, 0};
        }

        const auto bytes = static_cast<std::uint64_t>(st.st_size);
        if (::unlink(path.c_str()) != 0) {
            throw_errno(errno, path);
        }

        core_log::file_deleted(logger_, path, bytes);
        return Result{Operation::Delete, path, bytes};
    });
}

/// Runs `body` with timing and lifecycle logs, and maps raw file-system failures to Error with the
/// right exit code.
Result FileOperations::execute(Operation operation,
                               const std::filesystem::path& path,
                               const std::function<Result()>& body) {
    core_log::operation_started(logger_, operation, path);
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed_ms = [&] {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };

    try {
        Result result = body();
        core_log::operation_completed(logger_, operation, path, result.bytes, elapsed_ms());
        return result;
    } catch (const Error& e) {
        core_log::operation_failed(logger_, operation, path, e.code(), e.what());
        throw;
    } catch (const std::filesystem::filesystem_error& e) {
        const Error translated = translate(e, path);
        core_log::operation_failed(logger_, operation, path, translated.code(), translated.what());
        throw Error(translated);
    } catch (const std::exception& e) {
        const Error internal(ExitCode::InternalError,
                             std::format("unexpected error on {}: {}", path.string(), e.what()),
                             path);
        core_log::operation_failed(logger_, operation, path, internal.code(), internal.what());
        throw Error(internal);
    }
}

}  // namespace fileops
