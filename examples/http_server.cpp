#include "coroutine-runtime.h"

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <iostream>
#include <linux/openat2.h>
#include <memory>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

const size_t CHUNK_SIZE = 64 * 1024;
const size_t MAX_HEADER_SIZE = 8 * 1024;

struct Request {
    std::string_view method;
    std::string_view path;
    bool keep_alive;
};

static bool equals_ignore_case(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (std::tolower(a[i]) != std::tolower(b[i])) {
            return false;
        }
    }
    return true;
}

static std::optional<Request> parse_request(std::string_view head) {
    size_t eol = head.find("\r\n");
    std::string_view line = head.substr(0, eol);
    size_t sp1 = line.find(' ');
    size_t sp2 = line.rfind(' ');
    if (sp1 == std::string_view::npos || sp1 == sp2) {
        return std::nullopt;
    }
    std::string_view method = line.substr(0, sp1);
    std::string_view path = line.substr(sp1 + 1, sp2 - sp1 - 1);
    std::string_view version = line.substr(sp2 + 1);
    if (!version.starts_with("HTTP/1.")) {
        return std::nullopt;
    }

    bool keep_alive = false;
    while (eol != std::string_view::npos) {
        size_t start = eol + 2;
        eol = head.find("\r\n", start);
        std::string_view header = head.substr(start, eol == std::string_view::npos ? eol : eol - start);
        size_t colon = header.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        std::string_view key = header.substr(0, colon);
        std::string_view value = header.substr(colon + 1);
        while (value.starts_with(' ')) {
            value.remove_prefix(1);
        }
        if (equals_ignore_case(key, "connection")) {
            if (equals_ignore_case(value, "close")) {
                keep_alive = false;
            } else if (equals_ignore_case(value, "keep-alive")) {
                keep_alive = true;
            }
        }
    }
    return Request{method, path, keep_alive};
}

static std::string_view content_type(std::string_view path) {
    size_t dot = path.rfind('.');
    std::string_view ext = dot == std::string_view::npos ? "" : path.substr(dot + 1);
    if (ext == "html" || ext == "htm")
        return "text/html; charset=utf-8";
    if (ext == "css")
        return "text/css";
    if (ext == "js")
        return "text/javascript";
    if (ext == "json")
        return "application/json";
    if (ext == "txt" || ext == "md")
        return "text/plain; charset=utf-8";
    if (ext == "png")
        return "image/png";
    if (ext == "jpg" || ext == "jpeg")
        return "image/jpeg";
    return "application/octet-stream";
}

static bool send_all(Context& c, int fd, const char* data, size_t len) {
    for (size_t off = 0; off < len;) {
        int n = c.send(fd, data + off, len - off, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        off += n;
    }
    return true;
}

static bool send_error(Context& c, int fd, int status, std::string_view reason, bool keep_alive) {
    std::string body = std::format("{} {}\n", status, reason);
    std::string resp = std::format(
        "HTTP/1.1 {} {}\r\nContent-Type: text/plain\r\nContent-Length: {}\r\nConnection: {}\r\n\r\n{}",
        status,
        reason,
        body.size(),
        keep_alive ? "keep-alive" : "close",
        body
    );
    return send_all(c, fd, resp.data(), resp.size());
}

static bool serve_file(Context& c, int sock, int root_fd, const Request& req, char* buf) {
    if (req.method != "GET") {
        return send_error(c, sock, 405, "Method Not Allowed", req.keep_alive);
    }

    std::string path(req.path.substr(0, req.path.find('?')));
    if (path.ends_with('/')) {
        path += "index.html";
    }
    while (path.starts_with('/')) {
        path.erase(0, 1);
    }

    open_how how{.flags = O_RDONLY | O_CLOEXEC, .mode = 0, .resolve = RESOLVE_BENEATH};
    int file = c.openat2(root_fd, path.c_str(), &how);
    if (file < 0) {
        if (file == -ENOENT || file == -ENOTDIR || file == -EXDEV) {
            return send_error(c, sock, 404, "Not Found", req.keep_alive);
        }
        if (file == -EACCES) {
            return send_error(c, sock, 403, "Forbidden", req.keep_alive);
        }
        return send_error(c, sock, 500, "Internal Server Error", req.keep_alive);
    }

    struct statx st{};
    int ret = c.statx(file, "", AT_EMPTY_PATH, STATX_TYPE | STATX_SIZE, &st);
    if (ret < 0 || !S_ISREG(st.stx_mode)) {
        c.close(file);
        return send_error(c, sock, 404, "Not Found", req.keep_alive);
    }

    std::string head = std::format(
        "HTTP/1.1 200 OK\r\nContent-Type: {}\r\nContent-Length: {}\r\nConnection: {}\r\n\r\n",
        content_type(path),
        st.stx_size,
        req.keep_alive ? "keep-alive" : "close"
    );
    bool ok = send_all(c, sock, head.data(), head.size());

    for (uint64_t off = 0; ok && off < st.stx_size;) {
        int n = c.read(file, buf, CHUNK_SIZE, off);
        if (n <= 0) {
            ok = false;
            break;
        }
        ok = send_all(c, sock, buf, n);
        off += n;
    }
    c.close(file);
    return ok && req.keep_alive;
}

static void connection(Context& c, int sock, int root_fd) {
    auto buf = std::make_unique<char[]>(CHUNK_SIZE);
    std::string pending;

    while (true) {
        size_t header_end;
        size_t scan_from = 0;
        while ((header_end = pending.find("\r\n\r\n", scan_from)) == std::string::npos) {
            if (pending.size() > MAX_HEADER_SIZE) {
                send_error(c, sock, 431, "Request Header Fields Too Large", false);
                return;
            }
            scan_from = pending.size() >= 3 ? pending.size() - 3 : 0;
            int n = c.recv(sock, buf.get(), CHUNK_SIZE, 0);
            if (n <= 0) {
                return;
            }
            pending.append(buf.get(), n);
        }

        std::optional<Request> req = parse_request(std::string_view(pending).substr(0, header_end));
        if (!req) {
            send_error(c, sock, 400, "Bad Request", false);
            return;
        }
        if (!serve_file(c, sock, root_fd, *req, buf.get())) {
            return;
        }
        pending.erase(0, header_end + 4);
    }
}

int main(int argc, char** argv) {
    const char* root = argc > 1 ? argv[1] : ".";
    int root_fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (root_fd < 0) {
        std::cerr << "failed to open \"" << root << "\": " << root_fd << std::endl;
        return 1;
    }

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listen_fd != -1);
    int one = 1;
    int ret;
    ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    assert(ret == 0);
    sockaddr_in addr{.sin_family = AF_INET, .sin_port = htons(8080), .sin_addr = {INADDR_ANY}, .sin_zero = {}};
    ret = bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    assert(ret == 0);
    ret = listen(listen_fd, SOMAXCONN);
    assert(ret == 0);

    Context c;
    c.spawn([=](Context& c) {
        std::cerr << "serving " << root << " on :8080" << std::endl;
        while (true) {
            int sock = c.accept(listen_fd, nullptr, nullptr, 0);
            if (sock < 0) {
                std::cerr << "accept: " << std::strerror(-sock) << std::endl;
                continue;
            }
            c.spawn([=](Context& c) {
                connection(c, sock, root_fd);
                c.close(sock);
            });
        }
    });
    c.run_loop();
}
