#include "fileops/atomic_file.hpp"

#include "fileops/error.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <format>
#include <random>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fileops::atomic_file {
namespace {

// Most file systems cap a name at 255 bytes. The temp name wraps the destination name in "." and
// ".<16 hex>.tmp", so the name component is truncated to leave room for the decoration.
constexpr std::size_t max_name_bytes = 255;
constexpr std::size_t decoration_bytes = 1 + 1 + 16 + 4;

/// errno as text without strerror(3), whose static buffer is not thread safe.
std::string errno_text(int err) {
    return std::generic_category().message(err);
}

[[noreturn]] void throw_errno(int err, const std::filesystem::path& path, std::string_view action) {
    const std::string shown = path.string();
    switch (err) {
        case ENOENT:
            throw Error(ExitCode::NotFound, std::format("not found: {}", shown), path);
        case EEXIST:
            throw Error(ExitCode::AlreadyExists,
                        std::format("file already exists: {} (use --force to overwrite)", shown),
                        path);
        case EISDIR:
            throw Error(ExitCode::UsageError, std::format("is a directory: {}", shown), path);
        case EACCES:
        case EPERM:
            throw Error(ExitCode::IoError, std::format("permission denied: {}", shown), path);
        default:
            throw Error(ExitCode::IoError,
                        std::format("i/o error on {} ({}): {}", shown, action, errno_text(err)),
                        path);
    }
}

// Shortens a file name so the decorated temp name stays within the limit, cutting only on a UTF-8
// character boundary so a multi-byte sequence is never split.
std::string truncate_name(const std::string& name) {
    const std::size_t budget = max_name_bytes - decoration_bytes;
    if (name.size() <= budget) {
        return name;
    }
    std::size_t cut = budget;
    while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0U) == 0x80U) {
        --cut;  // inside a multi-byte sequence; back up to its lead byte
    }
    return name.substr(0, cut);
}

std::string random_hex16() {
    std::random_device device;
    const std::uint64_t value = (static_cast<std::uint64_t>(device()) << 32U) ^ device();
    return std::format("{:016x}", value);
}

// Owns the temp file until the rename succeeds: closes the descriptor and removes the file on any
// early exit, which is what makes a failure leave the destination untouched.
class TempFile {
public:
    TempFile(std::filesystem::path path, int fd) : path_(std::move(path)), fd_(fd) {}
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;
    ~TempFile() {
        close_fd();
        if (!committed_) {
            ::unlink(path_.c_str());
        }
    }

    [[nodiscard]] int fd() const noexcept { return fd_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    void close_fd() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    void committed() noexcept { committed_ = true; }

private:
    std::filesystem::path path_;
    int fd_;
    bool committed_ = false;
};

// Places the temp file at `destination` without replacing an existing file. link(2) is atomic and
// fails with EEXIST if the destination appeared in the meantime; file systems that refuse hard links
// fall back to an existence check plus rename.
void place_without_replacing(const std::filesystem::path& temp, const std::filesystem::path& destination) {
    if (::link(temp.c_str(), destination.c_str()) == 0) {
        ::unlink(temp.c_str());
        return;
    }
    const int err = errno;
    if (err == EEXIST) {
        throw_errno(EEXIST, destination, "link");
    }
    if (err != EPERM && err != EOPNOTSUPP && err != EXDEV && err != EMLINK) {
        throw_errno(err, destination, "link");
    }
    struct stat existing {};
    if (::stat(destination.c_str(), &existing) == 0) {
        throw_errno(EEXIST, destination, "rename");
    }
    if (::rename(temp.c_str(), destination.c_str()) != 0) {
        throw_errno(errno, destination, "rename");
    }
}

// The data was fsync'd through the descriptor; the new name lives in the directory, which has its
// own metadata to flush before the file can be called durable.
void sync_directory(const std::filesystem::path& directory, const std::filesystem::path& destination) {
    const int fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        throw_errno(errno, destination, "open directory");
    }
    const int rc = ::fsync(fd);
    const int err = errno;
    ::close(fd);
    // EINVAL: the file system does not support fsync on directories (some network and FUSE mounts).
    if (rc != 0 && err != EINVAL) {
        throw_errno(err, destination, "flush directory");
    }
}

}  // namespace

void write_all(int fd, const void* data, std::size_t size) {
    const auto* cursor = static_cast<const std::byte*>(data);
    std::size_t remaining = size;
    while (remaining > 0) {
        const ssize_t written = ::write(fd, cursor, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw Error(ExitCode::IoError, std::format("write failed: {}", errno_text(errno)));
        }
        cursor += written;
        remaining -= static_cast<std::size_t>(written);
    }
}

std::uint64_t write(const std::filesystem::path& destination,
                    bool overwrite,
                    const Writer& writer,
                    std::optional<mode_t> mode) {
    const std::filesystem::path full = std::filesystem::absolute(destination);
    const std::filesystem::path directory = full.parent_path();

    if (!std::filesystem::is_directory(directory)) {
        throw Error(
            ExitCode::NotFound, std::format("directory does not exist: {}", directory.string()), directory);
    }

    struct stat existing {};
    const bool exists = ::stat(full.c_str(), &existing) == 0;
    if (!overwrite && exists) {
        throw_errno(EEXIST, destination, "create");
    }

    // The bits go to open(2) so no observer ever sees the temp file wider than intended; fchmod
    // afterwards only restores bits the umask masked off.
    const bool explicit_mode = mode.has_value() || exists;
    const mode_t create_mode = mode.value_or(exists ? (existing.st_mode & 07777U) : 0666U);

    const std::filesystem::path temp_path =
        directory / std::format(".{}.{}.tmp", truncate_name(full.filename().string()), random_hex16());
    const int fd = ::open(temp_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, create_mode);
    if (fd < 0) {
        throw_errno(errno, destination, "create temporary file");
    }
    TempFile temp(temp_path, fd);

    if (explicit_mode && ::fchmod(temp.fd(), create_mode) != 0) {
        throw_errno(errno, destination, "set permissions");
    }

    writer(temp.fd());

    if (::fsync(temp.fd()) != 0) {
        throw_errno(errno, destination, "flush");
    }
    struct stat written {};
    if (::fstat(temp.fd(), &written) != 0) {
        throw_errno(errno, destination, "stat");
    }
    temp.close_fd();

    if (overwrite) {
        if (::rename(temp.path().c_str(), full.c_str()) != 0) {
            throw_errno(errno, destination, "rename");
        }
    } else {
        place_without_replacing(temp.path(), full);
    }
    temp.committed();
    sync_directory(directory, destination);
    return static_cast<std::uint64_t>(written.st_size);
}

}  // namespace fileops::atomic_file
