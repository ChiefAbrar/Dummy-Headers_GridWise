#pragma once
#include "types.hpp"
#include <string>
#include <vector>

namespace gridwise {

struct ReplayResult {
    bool ok{false};
    std::string error;
};

ReplayResult replay_and_validate(const Scenario& scenario, const std::vector<Directive>& directives, const Plan& plan);
std::string summarize_plan(const Scenario& scenario, const std::vector<Directive>& directives, const Plan& plan);

}