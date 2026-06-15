# Project: TCP Server v1.0
FROM ubuntu:20.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    qt5-default \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/server
COPY server.cpp .
RUN g++ -std=c++17 -pthread server.cpp -o tcp_server

FROM ubuntu:20.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/server
COPY --from=builder /app/server/tcp_server .

EXPOSE 12345
CMD ["./tcp_server"]
