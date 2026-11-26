#include "coroutine-runtime.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <netinet/in.h>

struct Event;

struct Message {
    int client_id;
    std::shared_ptr<const std::string> msg;
};

struct Join {
    int client_id;
    Sender<Event> inbox;
};

struct Leave {
    int client_id;
};

struct Event : std::variant<Join, Leave, Message> {
    using variant::variant;
};

void broker(Context& c, Receiver<Event> events) {
    (void)c;
    std::unordered_map<int, Sender<Event>> inboxes;
    auto broadcast = [&](const Event& ev) {
        for (auto& [id, inbox] : inboxes)
            inbox.send(ev);
    };
    while (auto ev = events.recv()) {
        if (auto* j = std::get_if<Join>(&*ev)) {
            inboxes.emplace(j->client_id, j->inbox);
        } else if (auto* l = std::get_if<Leave>(&*ev)) {
            inboxes.erase(l->client_id);
        }
        broadcast(*ev);
    }
}

Sender<std::string> socket_writer(Context& c, int fd) {
    auto [tx, rx] = make_channel<std::string>(c, 16);
    c.spawn([=](Context& c) mutable {
        while (auto str = rx.recv()) {
            const char* buf = str->data();
            size_t n = str->size();
            for (size_t off = 0; off < n;) {
                int m = c.send(fd, buf + off, n - off, 0);
                if (m <= 0) {
                    goto end;
                }
                off += m;
            }
        }
    end:
        (void)0;
    });
    return tx;
}

Receiver<std::string> socket_reader(Context& c, int fd) {
    auto [tx, rx] = make_channel<std::string>(c, 16);
    c.spawn([=](Context& c) mutable {
        char buf[4096];
        std::string message;
        while (true) {
            int n = c.recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            for (size_t off = 0; off < (size_t)n; ++off) {
                message.push_back(buf[off]);
                if (buf[off] == '\n') {
                    tx.send(std::move(message));
                    message.clear();
                }
            }
        }
        tx.close();
    });
    return rx;
};

void session(Context& c, int client_id, int fd, Sender<Event> notify) {
    auto [inbox_tx, inbox_rx] = make_channel<Event>(c, 16);
    notify.send(Join{client_id, inbox_tx});
    auto input = socket_reader(c, fd);
    auto output = socket_writer(c, fd);
    Selector s(c);
    std::optional<std::string> line;
    std::optional<Event> ev;
    size_t from_socket = s.recv(input, line);
    size_t from_broker = s.recv(inbox_rx, ev);
    while (auto branch = s.select()) {
        if (*branch == from_socket) {
            if (line) {
                notify.send(Message{client_id, std::make_shared<const std::string>(std::move(*line))});
            } else {
                break;
            }
        } else if (*branch == from_broker) {
            if (ev) {
                if (auto* j = std::get_if<Join>(&*ev)) {
                    output.send(std::format("client {} joined\n", j->client_id));
                } else if (auto* l = std::get_if<Leave>(&*ev)) {
                    output.send(std::format("client {} left\n", l->client_id));
                } else if (auto* m = std::get_if<Message>(&*ev)) {
                    output.send(std::format("client {} sent: {}", m->client_id, *m->msg));
                }
            }
        } else {
            std::cerr << "unexpected selection: " << *branch << std::endl;
            std::abort();
        }
    }
    notify.send(Leave{client_id});
    output.close();
}

void serve(Context& c) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(listen_fd != -1);
    int one = 1;
    int ret;
    ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    assert(ret == 0);
    sockaddr_in addr{.sin_family = AF_INET, .sin_port = htons(8080), .sin_addr = {INADDR_ANY}, .sin_zero = {}};
    ret = bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    assert(ret == 0);
    ret = listen(listen_fd, SOMAXCONN);
    assert(ret == 0);
    std::cerr << "listening on 8080" << std::endl;

    auto [events_tx, events_rx] = make_channel<Event>(c, 16);

    c.spawn([=](Context& c) { broker(c, events_rx); });

    for (int client_id = 1;; ++client_id) {
        int fd = c.accept(listen_fd, nullptr, nullptr, 0);
        if (fd < 0) {
            std::cerr << "accept error: " << std::strerror(-fd) << std::endl;
            continue;
        }
        c.spawn([=](Context& c) {
            session(c, client_id, fd, events_tx);
            c.close(fd);
        });
    };
}

int main() {
    Context c;
    c.spawn(serve);
    c.run_loop();
    return 0;
}
