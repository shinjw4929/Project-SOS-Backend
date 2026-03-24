#include "ratelimit/RateLimiter.h"
#include "redis/RedisClient.h"

namespace sos {

RateLimiter::RateLimiter(RedisClient& redis, int max_requests, std::chrono::seconds window,
                         std::string key_prefix)
    : redis_(redis), max_requests_(max_requests), window_(window),
      key_prefix_(std::move(key_prefix)) {}

bool RateLimiter::allow(const std::string& identifier) {
    std::string key = key_prefix_ + identifier;
    int64_t count = redis_.incr(key);

    // 첫 요청 시 TTL 설정 (윈도우 시작)
    if (count == 1) {
        redis_.expire(key, window_);
    }

    return count <= max_requests_;
}

} // namespace sos
