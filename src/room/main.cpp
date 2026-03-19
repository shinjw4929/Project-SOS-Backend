#include "util/Logger.h"
#include "util/Config.h"
#include "server/RoomServer.h"
#include "room/RoomManager.h"

#include <boost/asio.hpp>
#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdint>

int main(int argc, char* argv[]) {
    sos::Logger::init("room");

    uint16_t port = 8080;
    uint32_t max_rooms = 100;
    uint32_t max_players_per_room = 8;

    std::string config_path = (argc > 1) ? argv[1] : "config/server_config.json";
    try {
        sos::Config config(config_path);
        port = config.roomPort();
        max_rooms = config.maxRooms();
        max_players_per_room = config.maxPlayersPerRoom();
    } catch (const std::exception&) {
        spdlog::info("[Room] Config file not found, using defaults");
    }

    boost::asio::io_context io_context;

    auto room_manager = std::make_shared<sos::RoomManager>(max_rooms, max_players_per_room);
    sos::RoomServer server(io_context, port, room_manager);

    boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](boost::system::error_code, int signal_number) {
        spdlog::info("[Room] Signal {} received, shutting down...", signal_number);
        server.stop();
    });

    server.start();
    spdlog::info("[Room] Room Server started, port={}, max_rooms={}, max_players={}",
                 port, max_rooms, max_players_per_room);

    io_context.run();

    spdlog::info("[Room] Room Server stopped");
    return 0;
}
