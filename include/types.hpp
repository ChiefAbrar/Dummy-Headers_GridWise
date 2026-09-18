#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace gridwise {

constexpr std::size_t HOURS = 24;
constexpr double EPS = 1e-7;
constexpr double CHECK_TOL = 1e-2;

struct Hour {
    int hour{};
    double demand_kwh{};
    double solar_kwh{};
    double tariff_bdt_per_kwh{};
};

struct Battery {
    double capacity_kwh{};
    double initial_energy_kwh{};
    double minimum_energy_kwh{};
    double max_charge_kwh_per_hour{};
    double max_discharge_kwh_per_hour{};
};

struct Scenario {
    std::string scenario_id;
    std::vector<std::string> operator_notes;
    std::array<Hour, HOURS> hours{};
    Battery battery{};
};

enum class DirectiveType {
    SolarReduction,
    MinimumBatteryReserve,
    NoChargeWindow,
    NoDischargeWindow,
    MaxGridWindow,
    NoOp
};

inline std::string directive_name(DirectiveType t) {
    switch (t) {
        case DirectiveType::SolarReduction: return "solar_reduction";
        case DirectiveType::MinimumBatteryReserve: return "minimum_battery_reserve";
        case DirectiveType::NoChargeWindow: return "no_charge_window";
        case DirectiveType::NoDischargeWindow: return "no_discharge_window";
        case DirectiveType::MaxGridWindow: return "max_grid_window";
        case DirectiveType::NoOp: return "no_op";
    }
    return "no_op";
}

inline std::optional<DirectiveType> directive_type_from_name(const std::string& s) {
    if (s == "solar_reduction") return DirectiveType::SolarReduction;
    if (s == "minimum_battery_reserve") return DirectiveType::MinimumBatteryReserve;
    if (s == "no_charge_window") return DirectiveType::NoChargeWindow;
    if (s == "no_discharge_window") return DirectiveType::NoDischargeWindow;
    if (s == "max_grid_window") return DirectiveType::MaxGridWindow;
    if (s == "no_op") return DirectiveType::NoOp;
    return std::nullopt;
}

struct Directive {
    std::size_t note_index{};
    bool applies{false};
    DirectiveType type{DirectiveType::NoOp};
    std::vector<int> hours;
    double factor{0.0};
    double minimum_energy_kwh{0.0};
    double max_grid_kwh{0.0};
    std::string explanation;
};

struct PlanHour {
    int hour{};
    double grid_kwh{};
    double solar_used_kwh{};
    std::string battery_action;
    double battery_kwh{};
    double battery_energy_after_kwh{};
};

struct Plan {
    std::array<PlanHour, HOURS> hours{};
    double total_grid_kwh{};
    double total_cost_bdt{};
    double peak_grid_kwh{};
};

struct InterpretationResult {
    std::vector<Directive> directives;
};

struct EngineResult {
    InterpretationResult interpretation;
    Plan plan;
    std::string plan_summary;
};

inline bool finite_nonnegative(double x) {
    return std::isfinite(x) && x >= -EPS;
}

}