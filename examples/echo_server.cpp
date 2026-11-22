#include "coroutine-runtime.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{.sin_family = AF_INET, .sin_port = htons(8080), .sin_addr = {INADDR_ANY}};
    bind(sock_fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    listen(sock_fd, SOMAXCONN);

    Context c;
    c.spawn([sock_fd](Context& c) {
        while (true) {
            int fd = c.accept(sock_fd, nullptr, nullptr, 0);
            if (fd < 0)
                continue;
            c.spawn([fd](Context& c) {
                char buf[4096];
                while (true) {
                    int n = c.recv(fd, buf, sizeof(buf), 0);
                    if (n <= 0)
                        break;                        // 0 = client closed, <0 = error
                    for (ssize_t off = 0; off < n;) { // send may write fewer bytes than asked
                        ssize_t m = c.send(fd, buf + off, n - off);
                        if (m <= 0) {
                            close(fd);
                            return;
                        }
                        off += m;
                    }
                }
                close(fd);
            });
        }
    });
    c.run_loop();
}
