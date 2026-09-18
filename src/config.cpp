#include "config.hpp"
#include <cstdlib>
#include <stdexcept>

namespace gridwise {

std::string env_or(const char* name, const std::string& fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

int env_int(const char* name, int fallback, int lo, int hi) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    try {
        int x = std::stoi(v);
        if (x < lo || x > hi) throw std::out_of_range("range");
        return x;
    } catch (...) {
        return fallback;
    }
}

std::size_t env_size(const char* name, std::size_t fallback, std::size_t lo, std::size_t hi) {
    const char* v = std::getenv(name);
    if (!v || !*v) return fallback;
    try {
        auto x = static_cast<std::size_t>(std::stoull(v));
        if (x < lo || x > hi) throw std::out_of_range("range");
        return x;
    } catch (...) {
        return fallback;
    }
}

Config load_config() {
    Config c;
    c.bind_host = env_or("GRIDWISE_HOST", c.bind_host);
    c.port = env_int("PORT", c.port, 1, 65535);
    c.worker_threads = env_size("GRIDWISE_WORKERS", c.worker_threads, 1, 128);
    c.max_pending = env_size("GRIDWISE_MAX_PENDING", c.max_pending, 1, 4096);
    c.llm_concurrency = env_size("GRIDWISE_LLM_CONCURRENCY", c.llm_concurrency, 1, 64);
    c.llm_cache_entries = env_size("GRIDWISE_LLM_CACHE_ENTRIES", c.llm_cache_entries, 0, 4096);
    c.llm_timeout_ms = env_int("LLM_TIMEOUT_MS", c.llm_timeout_ms, 500, 25000);
    c.llm_connect_timeout_ms = env_int("LLM_CONNECT_TIMEOUT_MS", c.llm_connect_timeout_ms, 100, 10000);
    c.llm_retries = env_int("LLM_RETRIES", c.llm_retries, 0, 3);
    c.llm_base_url = env_or("LLM_BASE_URL", c.llm_base_url);
    c.llm_chat_path = env_or("LLM_CHAT_PATH", c.llm_chat_path);
    c.llm_api_key = env_or("LLM_API_KEY", "");
    c.llm_model = env_or("LLM_MODEL", "");
    c.llm_system = env_or("LLM_SYSTEM_PROMPT", "");
    return c;
}

}