find_package(Protobuf CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(hiredis CONFIG REQUIRED)
find_package(redis++ CONFIG REQUIRED)

set(PROTO_DIR "${CMAKE_SOURCE_DIR}/proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

set(PROTO_NAMES room chat)
set(PROTO_SRCS "")
set(PROTO_HDRS "")

foreach(name ${PROTO_NAMES})
    list(APPEND PROTO_SRCS "${PROTO_GEN_DIR}/${name}.pb.cc")
    list(APPEND PROTO_HDRS "${PROTO_GEN_DIR}/${name}.pb.h")

    add_custom_command(
        OUTPUT "${PROTO_GEN_DIR}/${name}.pb.cc" "${PROTO_GEN_DIR}/${name}.pb.h"
        COMMAND protobuf::protoc
            "--proto_path=${PROTO_DIR}"
            "--cpp_out=${PROTO_GEN_DIR}"
            "${PROTO_DIR}/${name}.proto"
        DEPENDS "${PROTO_DIR}/${name}.proto"
        COMMENT "Generating C++ from ${name}.proto"
    )
endforeach()

add_library(sos_common STATIC
    ${PROTO_SRCS}
    ${PROTO_HDRS}
    util/Logger.cpp
    util/Config.cpp
    util/UuidGenerator.cpp
    redis/RedisClient.cpp
    redis/SessionStore.cpp
    ratelimit/RateLimiter.cpp
)

target_include_directories(sos_common PUBLIC
    "${PROTO_GEN_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(sos_common PUBLIC
    protobuf::libprotobuf
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    hiredis::hiredis
    redis++::redis++
)
