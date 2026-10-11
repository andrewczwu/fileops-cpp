#include "tool.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

// The POSIX environment block, declared by the C library without a header.
// NOLINTNEXTLINE(readability-redundant-declaration,cppcoreguidelines-avoid-non-const-global-variables)
extern char** environ;

namespace fileops::test {
namespace {

constexpr int timeout_ms = 60'000;

// getenv is not thread safe in general; the test harness is single-threaded and never calls setenv.
std::string binary_path() {
    if (const char* bin = std::getenv("FILEOPS_BIN");
        bin != nullptr && *bin != '\0') {  // NOLINT(concurrency-mt-unsafe)
        return bin;
    }
    return FILEOPS_DEFAULT_BIN;
}

[[noreturn]] void fail(const std::string& what) {
    throw std::system_error(errno, std::generic_category(), what);
}

/// Reads both pipes until EOF, interleaving so a child that fills one pipe never blocks.
void drain(int out_fd, int err_fd, std::string& out, std::string& err) {
    std::array<char, 65536> buffer{};
    std::array<pollfd, 2> fds{{{out_fd, POLLIN, 0}, {err_fd, POLLIN, 0}}};
    int open_count = 2;
    while (open_count > 0) {
        const int ready = ::poll(fds.data(), fds.size(), timeout_ms);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            fail("poll");
        }
        if (ready == 0) {
            throw std::runtime_error("fileops produced no output for 60 s");
        }
        for (auto& fd : fds) {
            if (fd.fd < 0 || (fd.revents & (POLLIN | POLLHUP)) == 0) {
                continue;
            }
            const ssize_t n = ::read(fd.fd, buffer.data(), buffer.size());
            if (n > 0) {
                (fd.fd == out_fd ? out : err).append(buffer.data(), static_cast<std::size_t>(n));
            } else if (n == 0 || errno != EINTR) {
                fd.fd = -1;
                --open_count;
            }
        }
    }
}

/// Appends this run's log records to $FILEOPS_TEST_LOG_DIR/cli-runs.ndjson so CI can publish the
/// telemetry of every invocation and a failure can be diagnosed without re-running the suite.
void record_run(const std::vector<std::string>& args, const CliResult& result) {
    const char* directory = std::getenv("FILEOPS_TEST_LOG_DIR");  // NOLINT(concurrency-mt-unsafe)
    if (directory == nullptr || *directory == '\0') {
        return;
    }
    static std::mutex lock;
    const std::scoped_lock guard(lock);
    std::filesystem::create_directories(directory);
    std::ofstream out(std::filesystem::path(directory) / "cli-runs.ndjson", std::ios::app);
    out << nlohmann::json{{"run",
                           {{"args", args}, {"exitCode", result.exit_code}, {"binary", Tool::description()}}}}
               .dump()
        << '\n';
    for (const auto& line : result.stderr_lines()) {
        if (line.starts_with('{')) {
            out << line << '\n';
        }
    }
}

}  // namespace

std::vector<std::string> CliResult::stderr_lines() const {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < stderr_text.size()) {
        std::size_t end = stderr_text.find('\n', start);
        if (end == std::string::npos) {
            end = stderr_text.size();
        }
        if (end > start) {
            lines.push_back(stderr_text.substr(start, end - start));
        }
        start = end + 1;
    }
    return lines;
}

std::vector<nlohmann::json> CliResult::log_records() const {
    std::vector<nlohmann::json> records;
    for (const auto& line : stderr_lines()) {
        if (line.starts_with('{')) {
            records.push_back(nlohmann::json::parse(line));
        }
    }
    return records;
}

std::vector<std::string> CliResult::messages() const {
    std::vector<std::string> found;
    for (const auto& line : stderr_lines()) {
        if (line.starts_with("fileops:")) {
            found.push_back(line);
        }
    }
    return found;
}

nlohmann::json CliResult::log(int event_id) const {
    std::optional<nlohmann::json> found;
    for (const auto& record : log_records()) {
        if (record.at("EventId").get<int>() == event_id) {
            if (found) {
                throw std::runtime_error("more than one log record with event id " +
                                         std::to_string(event_id));
            }
            found = record;
        }
    }
    if (!found) {
        throw std::runtime_error("no log record with event id " + std::to_string(event_id) +
                                 "; stderr was:\n" + stderr_text);
    }
    return *found;
}

bool CliResult::has_log(int event_id) const {
    const auto records = log_records();
    return std::ranges::any_of(
        records, [&](const nlohmann::json& record) { return record.at("EventId").get<int>() == event_id; });
}

std::vector<int> CliResult::event_ids() const {
    std::vector<int> ids;
    for (const auto& record : log_records()) {
        ids.push_back(record.at("EventId").get<int>());
    }
    return ids;
}

std::string Tool::description() {
    return binary_path();
}

CliResult Tool::run(const std::filesystem::path& working_directory, const std::vector<std::string>& args) {
    return run(
        working_directory, Environment{{"FILEOPS_LOG_FORMAT", "json"}, {"FILEOPS_LOG_LEVEL", "Debug"}}, args);
}

CliResult Tool::run(const std::filesystem::path& working_directory,
                    const Environment& environment,
                    const std::vector<std::string>& args) {
    // Start from a clean slate so the host's settings cannot leak into assertions.
    std::vector<std::string> env_storage;
    for (char** entry = environ; *entry != nullptr; ++entry) {
        const std::string_view line{*entry};
        if (line.starts_with("FILEOPS_LOG_") ||
            environment.contains(std::string{line.substr(0, line.find('='))})) {
            continue;
        }
        env_storage.emplace_back(line);
    }
    for (const auto& [key, value] : environment) {
        if (value) {
            env_storage.push_back(key + "=" + *value);
        }
    }

    const std::string binary = binary_path();
    std::vector<std::string> arg_storage{binary};
    arg_storage.insert(arg_storage.end(), args.begin(), args.end());
    std::vector<char*> argv;
    argv.reserve(arg_storage.size() + 1);
    for (auto& a : arg_storage) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    std::vector<char*> envp;
    envp.reserve(env_storage.size() + 1);
    for (auto& e : env_storage) {
        envp.push_back(e.data());
    }
    envp.push_back(nullptr);

    std::array<int, 2> out_pipe{};
    std::array<int, 2> err_pipe{};
    if (::pipe(out_pipe.data()) != 0 || ::pipe(err_pipe.data()) != 0) {
        fail("pipe");
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        fail("fork");
    }
    if (pid == 0) {
        // Child: no heap allocation, no C++ exceptions, only async-signal-safe calls before exec.
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);
        if (::chdir(working_directory.c_str()) != 0) {
            ::_exit(126);
        }
        ::execve(binary.c_str(), argv.data(), envp.data());
        ::_exit(127);
    }

    ::close(out_pipe[1]);
    ::close(err_pipe[1]);
    CliResult result;
    try {
        drain(out_pipe[0], err_pipe[0], result.stdout_text, result.stderr_text);
    } catch (...) {
        ::kill(pid, SIGKILL);
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);
        ::waitpid(pid, nullptr, 0);
        throw;
    }
    ::close(out_pipe[0]);
    ::close(err_pipe[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            fail("waitpid");
        }
    }
    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
    if (result.exit_code == 127 && result.stdout_text.empty() && result.stderr_text.empty()) {
        throw std::runtime_error("could not execute " + binary);
    }
    record_run(args, result);
    return result;
}

}  // namespace fileops::test
