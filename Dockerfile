FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential pkg-config liburing-dev gdb \
 && rm -rf /var/lib/apt/lists/* \
 && echo 'set editing-mode vi' >> /etc/inputrc
WORKDIR /work
