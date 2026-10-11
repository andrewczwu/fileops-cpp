#pragma once

// A throw-away directory per test, removed on destruction. Also knows how to spot the atomic
// writer's temp files so tests can prove none are left behind.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace fileops::test {

class TempDirectory {
public:
    TempDirectory() {
        std::string pattern = (std::filesystem::temp_directory_path() / "fileops-test-XXXXXX").string();
        if (::mkdtemp(pattern.data()) == nullptr) {
            throw std::runtime_error("mkdtemp failed");
        }
        // Resolve symlinks (macOS puts /tmp under /private) so paths compare equal with what the
        // library reports.
        path_ = std::filesystem::canonical(pattern);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    TempDirectory(TempDirectory&&) = delete;
    TempDirectory& operator=(TempDirectory&&) = delete;
    ~TempDirectory() {
        std::error_code ignored;
        ::chmod(path_.c_str(), 0700);  // undo read-only experiments so removal works
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    [[nodiscard]] std::filesystem::path file(std::string_view name) const { return path_ / name; }

    std::filesystem::path write_file(std::string_view name, std::string_view content) const {
        const auto target = file(name);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out) {
            throw std::runtime_error("could not write " + target.string());
        }
        return target;
    }

    std::filesystem::path write_file(std::string_view name, const std::vector<unsigned char>& content) const {
        return write_file(name, std::string(content.begin(), content.end()));
    }

    /// Atomic-writer temp files (`.<name>.<hex>.tmp`) currently present; expected to be empty.
    [[nodiscard]] std::vector<std::filesystem::path> leftover_temp_files() const {
        std::vector<std::filesystem::path> found;
        for (const auto& entry : std::filesystem::directory_iterator(path_)) {
            const std::string name = entry.path().filename().string();
            if (name.starts_with('.') && name.ends_with(".tmp")) {
                found.push_back(entry.path());
            }
        }
        return found;
    }

private:
    std::filesystem::path path_;
};

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("could not read " + path.string());
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline mode_t permission_bits(const std::filesystem::path& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) {
        throw std::runtime_error("stat failed for " + path.string());
    }
    return st.st_mode & 0777U;
}

/// Root bypasses file permissions, so permission-denied tests cannot run as root (CI containers often do).
/// A macro because GTEST_SKIP() returns from the calling test body, which a function cannot do.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define FILEOPS_SKIP_IF_ROOT()                                                           \
    if (::geteuid() == 0) {                                                              \
        GTEST_SKIP() << "Requires an unprivileged user: root ignores file permissions."; \
    }                                                                                    \
    static_cast<void>(0)

}  // namespace fileops::test
