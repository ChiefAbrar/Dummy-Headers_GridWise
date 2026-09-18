#pragma once
#include <cstddef>
#include <string>

namespace gridwise {

struct Config {
    std::string bind_host = "0.0.0.0";
    int port = 8080;
    std::size_t worker_threads = 8;
    std::size_t max_pending = 16;
    std::size_t llm_concurrency = 4;
    std::size_t llm_cache_entries = 256;
    int llm_timeout_ms = 4500;
    int llm_connect_timeout_ms = 1200;
    int llm_retries = 0;
    std::string llm_base_url = "___";
    std::string llm_chat_path = "___";
    std::string llm_api_key;
    std::string llm_model = "";
    std::string llm_system = "";
};

Config load_config();
std::string env_or(const char* name, const std::string& fallback);
int env_int(const char* name, int fallback, int lo, int hi);
std::size_t env_size(const char* name, std::size_t fallback, std::size_t lo, std::size_t hi);

}