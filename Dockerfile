# === Build stage (공유) ===
FROM ubuntu:24.04 AS build-base
RUN apt-get update && apt-get install -y \
    build-essential g++ cmake ninja-build git curl zip unzip tar pkg-config \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git $VCPKG_ROOT && \
    $VCPKG_ROOT/bootstrap-vcpkg.sh

WORKDIR /app
COPY vcpkg.json ./
RUN $VCPKG_ROOT/vcpkg install --triplet x64-linux
COPY CMakeLists.txt CMakePresets.json ./
COPY proto/ proto/
COPY src/ src/
RUN cmake --preset=default && cmake --build build

# === Room Server ===
FROM ubuntu:24.04 AS room-server
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build-base /app/build/src/room/room-server /usr/local/bin/
COPY config/ /app/config/
WORKDIR /app
CMD ["room-server", "config/server_config.json"]

# === Chat Server ===
FROM ubuntu:24.04 AS chat-server
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build-base /app/build/src/chat/chat-server /usr/local/bin/
COPY config/ /app/config/
WORKDIR /app
CMD ["chat-server", "config/server_config.json"]
