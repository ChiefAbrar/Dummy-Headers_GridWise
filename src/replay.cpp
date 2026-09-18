#include "replay.hpp"
#include "validation.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace gridwise {
namespace {
struct EffectiveRules {
    std::array<double, HOURS> solar{};
    std::array<double, HOURS> min_energy{};
    std::array<bool, HOURS> no_charge{};
    std::array<bool, HOURS> no_discharge{};
    std::array<double, HOURS> max_grid{};
};

EffectiveRules rules(const Scenario& s, const std::vector<Directive>& dirs) {
    EffectiveRules r;
    for (std::size_t h = 0; h < HOURS; ++h) {
        r.solar[h] = s.hours[h].solar_kwh;
        r.min_energy[h] = s.battery.minimum_energy_kwh;
        r.max_grid[h] = std::numeric_limits<double>::infinity();
    }
    for (const auto& d : dirs) {
        if (!d.applies) continue;
        for (int hi : d.hours) {
            const auto h = static_cast<std::size_t>(hi);
            switch (d.type) {
                case DirectiveType::SolarReduction: r.solar[h] *= d.factor; break;
                case DirectiveType::MinimumBatteryReserve: r.min_energy[h] = std::max(r.min_energy[h], d.minimum_energy_kwh); break;
                case DirectiveType::NoChargeWindow: r.no_charge[h] = true; break;
                case DirectiveType::NoDischargeWindow: r.no_discharge[h] = true; break;
                case DirectiveType::MaxGridWindow: r.max_grid[h] = std::min(r.max_grid[h], d.max_grid_kwh); break;
                case DirectiveType::NoOp: break;
            }
        }
    }
    return r;
}

bool close_enough(double a, double b) { return std::abs(a - b) <= CHECK_TOL; }

}

ReplayResult replay_and_validate(const Scenario& s, const std::vector<Directive>& dirs, const Plan& plan) {
    auto vr = validate_directives(s, dirs);
    if (!vr.ok) return {false, vr.error};

    const auto r = rules(s, dirs);
    double energy = s.battery.initial_energy_kwh;
    double total_grid = 0.0;
    double total_cost = 0.0;
    double peak = 0.0;

    for (std::size_t h = 0; h < HOURS; ++h) {
        const auto& p = plan.hours[h];
        const auto& hr = s.hours[h];
        if (p.hour != static_cast<int>(h)) return {false, "hourly_plan hours are not exactly 0..23"};
        if (!finite_nonnegative(p.grid_kwh) || !finite_nonnegative(p.solar_used_kwh) || !finite_nonnegative(p.battery_kwh) ||
            !std::isfinite(p.battery_energy_after_kwh)) return {false, "plan contains invalid numeric values"};
        if (p.solar_used_kwh > r.solar[h] + CHECK_TOL) return {false, "solar usage exceeds effective solar"};
        if (p.battery_action != "charge" && p.battery_action != "discharge" && p.battery_action != "idle")
            return {false, "invalid battery action"};
        if (p.battery_action == "idle" && std::abs(p.battery_kwh) > CHECK_TOL) return {false, "idle action must have zero battery_kwh"};
        if (p.battery_action == "charge") {
            if (p.battery_kwh > s.battery.max_charge_kwh_per_hour + CHECK_TOL) return {false, "charge rate exceeded"};
            if (r.no_charge[h]) return {false, "charge forbidden by directive"};
        }
        if (p.battery_action == "discharge") {
            if (p.battery_kwh > s.battery.max_discharge_kwh_per_hour + CHECK_TOL) return {false, "discharge rate exceeded"};
            if (r.no_discharge[h]) return {false, "discharge forbidden by directive"};
        }
        if (p.battery_energy_after_kwh < r.min_energy[h] - CHECK_TOL || p.battery_energy_after_kwh > s.battery.capacity_kwh + CHECK_TOL)
            return {false, "battery bound violated"};
        if (std::isfinite(r.max_grid[h]) && p.grid_kwh > r.max_grid[h] + CHECK_TOL) return {false, "grid cap violated"};

        double next_energy = energy;
        if (p.battery_action == "charge") next_energy += p.battery_kwh;
        else if (p.battery_action == "discharge") next_energy -= p.battery_kwh;
        if (!close_enough(next_energy, p.battery_energy_after_kwh)) return {false, "battery transition mismatch"};

        const double lhs = p.grid_kwh + p.solar_used_kwh + (p.battery_action == "discharge" ? p.battery_kwh : 0.0);
        const double rhs = hr.demand_kwh + (p.battery_action == "charge" ? p.battery_kwh : 0.0);
        if (!close_enough(lhs, rhs)) return {false, "hourly energy balance failed"};

        energy = p.battery_energy_after_kwh;
        total_grid += p.grid_kwh;
        total_cost += p.grid_kwh * hr.tariff_bdt_per_kwh;
        peak = std::max(peak, p.grid_kwh);
    }

    if (!close_enough(energy, s.battery.initial_energy_kwh)) return {false, "end-of-day battery neutrality failed"};
    if (!close_enough(total_grid, plan.total_grid_kwh)) return {false, "total_grid_kwh disagrees with hourly_plan"};
    if (!close_enough(total_cost, plan.total_cost_bdt)) return {false, "total_cost_bdt disagrees with hourly_plan"};
    if (!close_enough(peak, plan.peak_grid_kwh)) return {false, "peak_grid_kwh disagrees with hourly_plan"};
    return {true, {}};
}

std::string summarize_plan(const Scenario&, const std::vector<Directive>& dirs, const Plan& plan) {
    std::size_t charge_hours = 0, discharge_hours = 0, solar_hours = 0;
    for (const auto& p : plan.hours) {
        if (p.battery_action == "charge") ++charge_hours;
        else if (p.battery_action == "discharge") ++discharge_hours;
        if (p.solar_used_kwh > 1e-7) ++solar_hours;
    }
    std::ostringstream out;
    std::size_t active = 0;
    for (const auto& d : dirs) if (d.applies) ++active;
    out << "Applies " << active << " operator directive" << (active == 1 ? "" : "s")
        << "; uses solar in " << solar_hours << " hour" << (solar_hours == 1 ? "" : "s")
        << ", charges in " << charge_hours << " hour" << (charge_hours == 1 ? "" : "s")
        << ", and discharges in " << discharge_hours << " hour" << (discharge_hours == 1 ? "" : "s")
        << ". The final battery energy is restored to its initial level while minimizing grid electricity cost.";
    return out.str();
}

}