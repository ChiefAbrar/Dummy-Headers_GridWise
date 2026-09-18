#pragma once
#include "config.hpp"
namespace gridwise { class LlmClient; }
#include "types.hpp"
#include <boost/json.hpp>
#include <memory>

namespace gridwise {

class Engine {
public:
    explicit Engine(const Config& config);
    ~Engine();
    boost::json::value run_json(const boost::json::value& request);
private:
    Config config_;
    std::unique_ptr<LlmClient> llm_;
};

}