# Multi-stage Dockerfile for C++ Backtest Overfit Engine on Render
# Stage 1: Build the C++ executable
FROM debian:bookworm-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libpq-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy build configuration and source code
COPY CMakeLists.txt .
COPY include/ ./include/
COPY src/ ./src/

# Compile in Release mode with maximum optimizations
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --config Release -j$(nproc)

# Stage 2: Minimal runtime image
FROM debian:bookworm-slim AS runner

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libpq5 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy binary from builder
COPY --from=builder /build/build/dsr_engine /app/dsr_engine

# Copy static assets and data
COPY web/ /app/web/
COPY data/ /app/data/
COPY schema.sql /app/schema.sql

# Ensure executable permissions
RUN chmod +x /app/dsr_engine

# Render assigns port dynamically via PORT env variable (default: 10000 on Render)
ENV PORT=10000
EXPOSE 10000

CMD ["./dsr_engine", "--server"]
