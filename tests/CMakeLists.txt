find_package(Catch2 3 CONFIG REQUIRED)

add_executable(unit-tests
    test_Codec.cpp
    test_UuidGenerator.cpp
    test_Config.cpp
    test_Room.cpp
    "${CMAKE_SOURCE_DIR}/src/room/room/Room.cpp"
)

target_include_directories(unit-tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/room"
)

target_link_libraries(unit-tests PRIVATE
    Catch2::Catch2WithMain
    sos_common
)

if(WIN32)
    target_compile_definitions(unit-tests PRIVATE _WIN32_WINNT=0x0A00)
endif()

include(CTest)
add_test(NAME unit-tests COMMAND unit-tests)
