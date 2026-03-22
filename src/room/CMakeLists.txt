find_package(Boost REQUIRED COMPONENTS system)

add_executable(room-server
    main.cpp
    server/RoomServer.cpp
    server/ClientSession.cpp
    room/Room.cpp
    room/RoomManager.cpp
    internal/GameServerChannel.cpp
    internal/GameServerSession.cpp
)

target_include_directories(room-server PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(room-server PRIVATE sos_common Boost::system)

if(WIN32)
    target_compile_definitions(room-server PRIVATE _WIN32_WINNT=0x0A00)
endif()
