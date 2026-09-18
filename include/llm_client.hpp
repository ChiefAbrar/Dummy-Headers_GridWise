#pragma once
#include "config.hpp"
#include "types.hpp"
#include <boost/json.hpp>
#include <string>
#include <list>
#include <mutex>
#include <unordered_map>
#include <semaphore>
#include <memory>

namespace gridwise {

class LlmClient {
public:
    explicit LlmClient(const Config& config);
    boost::json::value interpret_json(const Scenario& scenario);
    InterpretationResult interpret(const Scenario& scenario);

private:
    const Config& config_;
    std::string system_prompt() const;
    std::string make_cache_key(const Scenario& scenario) const;
    mutable std::mutex cache_mutex_;
    std::unordered_map<std::string, std::pair<std::string, std::list<std::string>::iterator>> cache_;
    std::list<std::string> lru_;
    std::unique_ptr<std::counting_semaphore<64>> llm_slots_;
};

}