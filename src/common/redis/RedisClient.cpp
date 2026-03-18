#include "redis/RedisClient.h"

#include <sw/redis++/redis++.h>

namespace sos {

struct RedisClient::Impl {
    sw::redis::Redis redis;

    Impl(const std::string& host, uint16_t port, const std::string& password)
        : redis(makeOptions(host, port, password)) {}

    static sw::redis::ConnectionOptions makeOptions(
        const std::string& host, uint16_t port, const std::string& password) {
        sw::redis::ConnectionOptions opts;
        opts.host = host;
        opts.port = static_cast<int>(port);
        if (!password.empty()) {
            opts.password = password;
        }
        return opts;
    }
};

RedisClient::RedisClient(const std::string& host, uint16_t port, const std::string& password)
    : impl_(std::make_unique<Impl>(host, port, password)) {}

RedisClient::~RedisClient() = default;
RedisClient::RedisClient(RedisClient&&) noexcept = default;
RedisClient& RedisClient::operator=(RedisClient&&) noexcept = default;

std::optional<std::string> RedisClient::get(const std::string& key) {
    auto result = impl_->redis.get(key);
    if (result) return *result;
    return std::nullopt;
}

void RedisClient::set(const std::string& key, const std::string& value) {
    impl_->redis.set(key, value);
}

void RedisClient::setex(const std::string& key, const std::string& value, std::chrono::seconds ttl) {
    impl_->redis.setex(key, ttl, value);
}

bool RedisClient::del(const std::string& key) {
    return impl_->redis.del(key) > 0;
}

int64_t RedisClient::incr(const std::string& key) {
    return impl_->redis.incr(key);
}

bool RedisClient::expire(const std::string& key, std::chrono::seconds ttl) {
    return impl_->redis.expire(key, ttl);
}

} // namespace sos
