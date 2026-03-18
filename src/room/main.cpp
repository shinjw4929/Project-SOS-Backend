#include "util/Logger.h"

#include <spdlog/spdlog.h>

int main() {
    sos::Logger::init("room");
    spdlog::info("[Room] Room Server starting...");
    return 0;
}
