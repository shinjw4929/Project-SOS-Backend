#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sos {

class RedisClient {
public:
    RedisClient(const std::string& host, uint16_t port, const std::string& password = "");
    ~RedisClient();

    RedisClient(RedisClient&&) noexcept;
    RedisClient& operator=(RedisClient&&) noexcept;

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    std::optional<std::string> get(const std::string& key);
    void set(const std::string& key, const std::string& value);
    void setex(const std::string& key, const std::string& value, std::chrono::seconds ttl);
    bool del(const std::string& key);
    int64_t incr(const std::string& key);
    bool expire(const std::string& key, std::chrono::seconds ttl);

    int64_t sadd(const std::string& key, const std::string& member);
    int64_t srem(const std::string& key, const std::string& member);
    int64_t scard(const std::string& key);
    bool exists(const std::string& key);

    // HASH
    void hset(const std::string& key, const std::string& field, const std::string& value);
    std::optional<std::string> hget(const std::string& key, const std::string& field);
    std::unordered_map<std::string, std::string> hgetall(const std::string& key);
    bool hdel(const std::string& key, const std::string& field);

    // LIST
    int64_t lpush(const std::string& key, const std::string& value);
    void ltrim(const std::string& key, int64_t start, int64_t stop);
    std::vector<std::string> lrange(const std::string& key, int64_t start, int64_t stop);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sos
