#include "otlp_receiver.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <system_error>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fileops::test {
namespace {

[[noreturn]] void fail(const char* what) {
    throw std::system_error(errno, std::generic_category(), what);
}

std::string lower(std::string s) {
    std::transform(
        s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Value of a header (case-insensitive name) in a raw header block, or "".
std::string header(const std::string& headers, const std::string& name) {
    const std::string lowered = lower(headers);
    const std::string needle = "\r\n" + lower(name) + ":";
    const auto at = lowered.find(needle);
    if (at == std::string::npos) {
        return "";
    }
    const auto start = at + needle.size();
    const auto end = lowered.find("\r\n", start);
    std::string value = headers.substr(start, end - start);
    value.erase(0, value.find_first_not_of(' '));
    return value;
}

bool read_exact(int fd, std::string& out, std::size_t count) {
    std::array<char, 65536> buffer{};
    while (out.size() < count) {
        const ssize_t n = ::read(fd, buffer.data(), std::min(buffer.size(), count - out.size()));
        if (n <= 0) {
            return false;
        }
        out.append(buffer.data(), static_cast<std::size_t>(n));
    }
    return true;
}

}  // namespace

OtlpReceiver::OtlpReceiver() {
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0) {
        fail("socket");
    }
    const int yes = 1;
    ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;                                                                 // any free port
    if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0) {  // NOLINT
        fail("bind");
    }
    socklen_t length = sizeof address;
    if (::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) != 0) {  // NOLINT
        fail("getsockname");
    }
    port_ = ntohs(address.sin_port);
    if (::listen(listener_, 16) != 0) {
        fail("listen");
    }
    thread_ = std::thread([this] { serve(); });
}

OtlpReceiver::~OtlpReceiver() {
    stop();
}

std::vector<ReceivedRequest> OtlpReceiver::stop() {
    if (!stopping_.exchange(true)) {
        ::shutdown(listener_, SHUT_RDWR);
        ::close(listener_);
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    const std::scoped_lock guard(lock_);
    return requests_;
}

std::vector<ReceivedRequest> OtlpReceiver::with_path(const std::vector<ReceivedRequest>& all,
                                                     const std::string& suffix) {
    std::vector<ReceivedRequest> found;
    for (const auto& r : all) {
        if (r.path.ends_with(suffix)) {
            found.push_back(r);
        }
    }
    return found;
}

void OtlpReceiver::serve() {
    while (!stopping_) {
        const int client = ::accept(listener_, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;  // listener closed by stop()
        }
        handle(client);
        ::close(client);
    }
}

/// One HTTP/1.1 request per connection (the exporter sends Connection: close or we close after
/// answering); the body is read to Content-Length and the response is an empty 200.
void OtlpReceiver::handle(int client) {
    std::string raw;
    std::size_t header_end = std::string::npos;
    std::array<char, 4096> buffer{};
    while (header_end == std::string::npos) {
        const ssize_t n = ::read(client, buffer.data(), buffer.size());
        if (n <= 0) {
            return;
        }
        raw.append(buffer.data(), static_cast<std::size_t>(n));
        header_end = raw.find("\r\n\r\n");
    }
    const std::string headers = raw.substr(0, header_end + 2);
    std::string body = raw.substr(header_end + 4);
    const auto length = static_cast<std::size_t>(
        std::stoul(header(headers, "Content-Length").empty() ? "0" : header(headers, "Content-Length")));
    if (!read_exact(client, body, length)) {
        return;
    }

    const auto first_space = headers.find(' ');
    const auto second_space = headers.find(' ', first_space + 1);
    ReceivedRequest request{headers.substr(first_space + 1, second_space - first_space - 1),
                            header(headers, "Content-Type"),
                            body.substr(0, length)};
    {
        const std::scoped_lock guard(lock_);
        requests_.push_back(std::move(request));
    }
    const std::string response = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    (void)::write(client, response.data(), response.size());
}

}  // namespace fileops::test
