#pragma once

#include <chrono>
#include <string>

namespace sos {

class RedisClient;

class RateLimiter {
public:
    RateLimiter(RedisClient& redis, int max_requests, std::chrono::seconds window);

    // true = 허용, false = 제한 초과
    bool allow(const std::string& identifier);

private:
    RedisClient& redis_;
    int max_requests_;
    std::chrono::seconds window_;
};

} // namespace sos
