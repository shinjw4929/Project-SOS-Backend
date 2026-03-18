#include "util/Logger.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/pattern_formatter.h>

namespace sos {

// Vector 파싱 정규식과 일치하는 대문자 레벨 (7칸 좌측 정렬)
// 출력 예: "2026-03-18 10:00:01.100 | INFO    | [Room] message"
class UppercaseLevelFlag : public spdlog::custom_flag_formatter {
public:
    void format(const spdlog::details::log_msg& msg, const std::tm&, spdlog::memory_buf_t& dest) override {
        static constexpr const char* names[] = {
            "TRACE  ", "DEBUG  ", "INFO   ", "WARNING", "ERROR  ", "CRITICAL", "OFF    "
        };
        auto name = names[static_cast<int>(msg.level)];
        dest.append(std::string_view(name));
    }

    std::unique_ptr<custom_flag_formatter> clone() const override {
        return std::make_unique<UppercaseLevelFlag>();
    }
};

void Logger::init(const std::string& service_name) {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>(service_name, console_sink);

    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter->add_flag<UppercaseLevelFlag>('*');
    formatter->set_pattern("%Y-%m-%d %H:%M:%S.%e | %* | %v");
    logger->set_formatter(std::move(formatter));

    logger->set_level(spdlog::level::debug);
    spdlog::set_default_logger(logger);
}

} // namespace sos
