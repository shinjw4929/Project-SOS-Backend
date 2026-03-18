#pragma once

#include <string>

namespace sos {

// Logger::init() 호출 후 spdlog::info(), spdlog::error() 등을 직접 사용
class Logger {
public:
    static void init(const std::string& service_name);
};

} // namespace sos
