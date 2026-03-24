find_package(Boost REQUIRED COMPONENTS system)

add_executable(chat-server
    main.cpp
    server/ChatServer.cpp
    server/ChatSession.cpp
    channel/ChannelManager.cpp
    internal/InternalChannel.cpp
    internal/InternalSession.cpp
)

target_include_directories(chat-server PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(chat-server PRIVATE sos_common Boost::system)

if(WIN32)
    target_compile_definitions(chat-server PRIVATE _WIN32_WINNT=0x0A00)
endif()
