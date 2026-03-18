#include "util/Logger.h"

#include <spdlog/spdlog.h>

int main() {
    sos::Logger::init("chat");
    spdlog::info("[Chat] Chat Server starting...");
    return 0;
}
