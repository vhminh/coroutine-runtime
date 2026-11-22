#include "coroutine-runtime.h"

#include <cassert>
#include <cerrno>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(sock_fd != -1);
    int one = 1;
    int ret;
    ret = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    assert(ret == 0);
    sockaddr_in addr{.sin_family = AF_INET, .sin_port = htons(8080), .sin_addr = {INADDR_ANY}};
    ret = bind(sock_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    assert(ret == 0);
    ret = listen(sock_fd, SOMAXCONN);
    assert(ret == 0);

    Context c;
    c.spawn([sock_fd](Context& c) {
        std::cout << "listening on 8080" << std::endl;
        while (true) {
            int fd = c.accept(sock_fd, nullptr, nullptr, 0);
            if (fd < 0) {
                std::cout << "accept error: " << errno << std::endl;
                continue;
            }
            std::cout << "accepted new connection: " << fd << std::endl;
            c.spawn([fd](Context& c) {
                char buf[4096];
                while (true) {
                    int n = c.recv(fd, buf, sizeof(buf), 0);
                    std::cout << "[" << fd << "] received " << n << " bytes: " << buf << std::endl;
                    if (n <= 0) {
                        std::cout << "[" << fd << "] recv error: " << errno << std::endl;
                        goto end;
                    }
                    for (ssize_t off = 0; off < n;) {
                        int m = c.send(fd, buf + off, n - off, 0);
                        if (m <= 0) {
                            std::cout << "[" << fd << "] send error: " << errno << std::endl;
                            goto end;
                        }
                        off += m;
                    }
                }
            end:
                close(fd);
            });
        }
    });
    c.run_loop();
}
