# Multi-stage Dockerfile for CACZ C++20 Server (Render Deployment)
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    g++ \
    cmake \
    ninja-build \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy CMake configuration and source code
COPY CMakeLists.txt ./
COPY include/ ./include/
COPY src/ ./src/

# Build native C++20 server binary
RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build --target cacz

# Stage 2: Minimal Runtime container
FROM debian:bookworm-slim AS runner

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy executable from builder
COPY --from=builder /src/build/cacz /usr/local/bin/cacz

# Default port (Render overrides this with $PORT)
ENV PORT=8080
EXPOSE 8080

CMD ["cacz", "server"]
