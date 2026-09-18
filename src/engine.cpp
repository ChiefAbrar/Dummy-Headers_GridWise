#include "engine.hpp"
#include "llm_client.hpp"
#include "optimizer.hpp"
#include "replay.hpp"
#include "validation.hpp"
#include <boost/json.hpp>
#include <cmath>
#include <stdexcept>

namespace gridwise {
namespace {


}

Engine::Engine(const Config& config) : config_(config), llm_(std::make_unique<LlmClient>(config_)) {}
Engine::~Engine() = default;

boost::json::value Engine::run_json(const boost::json::value& request) {
    Scenario scenario;
    auto iv = parse_scenario(request, scenario);
    if (!iv.ok) throw std::invalid_argument(iv.error);

    InterpretationResult interpretations = llm_->interpret(scenario);
    auto dv = validate_directives(scenario, interpretations.directives);
    if (!dv.ok) throw std::runtime_error("directive validation failed");
    Plan plan = optimize(scenario, interpretations.directives);
    auto replay = replay_and_validate(scenario, interpretations.directives, plan);
    if (!replay.ok) throw std::runtime_error("post-optimization replay validation failed");

    boost::json::object out;
    out["scenario_id"] = scenario.scenario_id;
    out["directive_interpretation"] = directives_to_json(interpretations);
    boost::json::array arr;
    for (const auto& p : plan.hours) {
        arr.push_back(boost::json::object{
            {"hour", p.hour},
            {"grid_kwh", p.grid_kwh},
            {"solar_used_kwh", p.solar_used_kwh},
            {"battery_action", p.battery_action},
            {"battery_kwh", p.battery_kwh},
            {"battery_energy_after_kwh", p.battery_energy_after_kwh}
        });
    }
    out["hourly_plan"] = std::move(arr);
    out["total_grid_kwh"] = plan.total_grid_kwh;
    out["total_cost_bdt"] = plan.total_cost_bdt;
    out["peak_grid_kwh"] = plan.peak_grid_kwh;
    out["plan_summary"] = summarize_plan(scenario, interpretations.directives, plan);
    return out;
}

}