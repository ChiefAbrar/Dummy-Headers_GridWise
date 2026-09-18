#include "validation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace gridwise {
namespace {

const boost::json::object* obj_ptr(const boost::json::value& v) {
    return v.is_object() ? &v.as_object() : nullptr;
}

bool get_string(const boost::json::object& o, const char* key, std::string& out) {
    auto* v = o.if_contains(key);
    if (!v || !v->is_string()) return false;
    out = std::string(v->as_string());
    return true;
}

bool get_number(const boost::json::object& o, const char* key, double& out) {
    auto* v = o.if_contains(key);
    if (!v || !v->is_number()) return false;
    if (v->is_double()) out = v->as_double();
    else if (v->is_int64()) out = static_cast<double>(v->as_int64());
    else if (v->is_uint64()) out = static_cast<double>(v->as_uint64());
    else return false;
    return std::isfinite(out);
}

bool get_integer(const boost::json::object& o, const char* key, int& out) {
    auto* v = o.if_contains(key);
    if (!v || !v->is_int64()) return false;
    const auto n = v->as_int64();
    if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max()) return false;
    out = static_cast<int>(n);
    return true;
}

bool parse_hours(const boost::json::object& o, std::vector<int>& hours) {
    auto* v = o.if_contains("hours");
    if (!v || !v->is_array()) return false;
    hours.clear();
    for (const auto& item : v->as_array()) {
        if (!item.is_int64()) return false;
        auto x = item.as_int64();
        if (x < 0 || x >= static_cast<std::int64_t>(HOURS)) return false;
        hours.push_back(static_cast<int>(x));
    }
    for (std::size_t i = 1; i < hours.size(); ++i) {
        if (hours[i] <= hours[i - 1]) return false;
    }
    return true;
}

bool parse_directive(const Scenario& s, const boost::json::value& v, Directive& out) {
    const auto* o = obj_ptr(v);
    if (!o) return false;

    auto* note_index = o->if_contains("note_index");
    auto* applies = o->if_contains("applies");
    auto* typev = o->if_contains("directive_type");
    if (!note_index || !note_index->is_int64() || !applies || !applies->is_bool() ||
        !typev || !typev->is_string()) return false;

    const auto idx = note_index->as_int64();
    if (idx < 0 || idx >= static_cast<std::int64_t>(s.operator_notes.size())) return false;
    auto type_opt = directive_type_from_name(std::string(typev->as_string()));
    if (!type_opt) return false;

    out = Directive{};
    out.note_index = static_cast<std::size_t>(idx);
    out.applies = applies->as_bool();
    out.type = *type_opt;

    if (out.type == DirectiveType::NoOp) {
        auto* adj = o->if_contains("structured_adjustment");
        if (out.applies || !adj || !adj->is_null()) return false;
        if (auto* e = o->if_contains("explanation"); e && e->is_string()) out.explanation = std::string(e->as_string());
        if (out.explanation.empty()) out.explanation = "This note does not affect the current energy schedule.";
        return true;
    }

    if (!out.applies) return false;
    auto* adjv = o->if_contains("structured_adjustment");
    if (!adjv || !adjv->is_object()) return false;
    const auto& adj = adjv->as_object();
    if (!parse_hours(adj, out.hours)) return false;

    switch (out.type) {
        case DirectiveType::SolarReduction: {
            if (!get_number(adj, "factor", out.factor) || out.factor < -EPS || out.factor > 1.0 + EPS) return false;
            out.factor = std::clamp(out.factor, 0.0, 1.0);
            break;
        }
        case DirectiveType::MinimumBatteryReserve: {
            if (!get_number(adj, "minimum_energy_kwh", out.minimum_energy_kwh) ||
                out.minimum_energy_kwh < -EPS || out.minimum_energy_kwh > s.battery.capacity_kwh + EPS) return false;
            out.minimum_energy_kwh = std::clamp(out.minimum_energy_kwh, 0.0, s.battery.capacity_kwh);
            break;
        }
        case DirectiveType::MaxGridWindow: {
            if (!get_number(adj, "max_grid_kwh", out.max_grid_kwh) || out.max_grid_kwh < -EPS) return false;
            out.max_grid_kwh = std::max(0.0, out.max_grid_kwh);
            break;
        }
        case DirectiveType::NoChargeWindow:
        case DirectiveType::NoDischargeWindow:
            break;
        case DirectiveType::NoOp:
            return false;
    }

    if (auto* e = o->if_contains("explanation"); e && e->is_string()) out.explanation = std::string(e->as_string());
    if (out.explanation.empty()) out.explanation = "Applied as a validated operator directive.";
    return true;
}

}

ValidationResult parse_scenario(const boost::json::value& v, Scenario& out) {
    const auto* o = obj_ptr(v);
    if (!o) return {false, "request must be a JSON object"};

    if (!get_string(*o, "scenario_id", out.scenario_id) || out.scenario_id.empty() || out.scenario_id.size() > 128)
        return {false, "scenario_id must be a non-empty string"};

    auto* notes = o->if_contains("operator_notes");
    if (!notes || !notes->is_array() || notes->as_array().size() < 1 || notes->as_array().size() > 3)
        return {false, "operator_notes must contain 1 to 3 strings"};
    out.operator_notes.clear();
    for (const auto& n : notes->as_array()) {
        if (!n.is_string()) return {false, "operator_notes must contain strings"};
        const std::string s = std::string(n.as_string());
        if (s.empty() || s.size() > 2000) return {false, "operator note is empty or too long"};
        out.operator_notes.push_back(s);
    }

    auto* hours = o->if_contains("hours");
    if (!hours || !hours->is_array() || hours->as_array().size() != HOURS)
        return {false, "hours must contain exactly 24 entries"};
    for (std::size_t i = 0; i < HOURS; ++i) {
        const auto* h = obj_ptr(hours->as_array()[i]);
        if (!h) return {false, "each hour must be an object"};
        int hour = -1; double demand = 0, solar = 0, tariff = 0;
        if (!get_integer(*h, "hour", hour) || hour != static_cast<int>(i)) return {false, "hours must be ordered 0 through 23"};
        if (!get_number(*h, "demand_kwh", demand) || demand < -EPS) return {false, "demand_kwh must be finite and non-negative"};
        if (!get_number(*h, "solar_kwh", solar) || solar < -EPS) return {false, "solar_kwh must be finite and non-negative"};
        if (!get_number(*h, "tariff_bdt_per_kwh", tariff)) return {false, "tariff_bdt_per_kwh must be finite"};
        out.hours[i] = Hour{hour, std::max(0.0, demand), std::max(0.0, solar), tariff};
    }

    auto* battery = o->if_contains("battery");
    if (!battery || !battery->is_object()) return {false, "battery must be an object"};
    const auto& b = battery->as_object();
    if (!get_number(b, "capacity_kwh", out.battery.capacity_kwh) || out.battery.capacity_kwh <= EPS)
        return {false, "capacity_kwh must be positive"};
    if (!get_number(b, "initial_energy_kwh", out.battery.initial_energy_kwh) ||
        !get_number(b, "minimum_energy_kwh", out.battery.minimum_energy_kwh) ||
        !get_number(b, "max_charge_kwh_per_hour", out.battery.max_charge_kwh_per_hour) ||
        !get_number(b, "max_discharge_kwh_per_hour", out.battery.max_discharge_kwh_per_hour))
        return {false, "battery numeric fields are invalid"};
    if (out.battery.initial_energy_kwh < -EPS || out.battery.initial_energy_kwh > out.battery.capacity_kwh + EPS ||
        out.battery.minimum_energy_kwh < -EPS || out.battery.minimum_energy_kwh > out.battery.capacity_kwh + EPS ||
        out.battery.max_charge_kwh_per_hour < -EPS || out.battery.max_discharge_kwh_per_hour < -EPS)
        return {false, "battery bounds or rates are invalid"};
    out.battery.initial_energy_kwh = std::clamp(out.battery.initial_energy_kwh, 0.0, out.battery.capacity_kwh);
    out.battery.minimum_energy_kwh = std::clamp(out.battery.minimum_energy_kwh, 0.0, out.battery.capacity_kwh);
    out.battery.max_charge_kwh_per_hour = std::max(0.0, out.battery.max_charge_kwh_per_hour);
    out.battery.max_discharge_kwh_per_hour = std::max(0.0, out.battery.max_discharge_kwh_per_hour);

    if (out.battery.initial_energy_kwh + EPS < out.battery.minimum_energy_kwh)
        return {false, "initial energy is below the base reserve"};

    return {true, {}};
}

ValidationResult validate_directives(const Scenario& s, const boost::json::value& v, InterpretationResult& out) {
    if (!v.is_object()) return {false, "LLM output must be an object"};
    auto* arrv = v.as_object().if_contains("interpretations");
    if (!arrv || !arrv->is_array()) return {false, "LLM output must contain interpretations"};
    const auto& arr = arrv->as_array();
    if (arr.size() != s.operator_notes.size()) return {false, "LLM must return exactly one interpretation per note"};

    out.directives.clear();
    out.directives.reserve(arr.size());
    std::set<std::size_t> seen;
    for (const auto& item : arr) {
        Directive d;
        if (!parse_directive(s, item, d)) return {false, "LLM directive failed deterministic guardrails"};
        if (!seen.insert(d.note_index).second) return {false, "duplicate note_index"};
        out.directives.push_back(std::move(d));
    }
    std::sort(out.directives.begin(), out.directives.end(), [](const Directive& a, const Directive& b) {
        return a.note_index < b.note_index;
    });
    for (std::size_t i = 0; i < out.directives.size(); ++i) {
        if (out.directives[i].note_index != i) return {false, "note_index order is invalid"};
    }
    return validate_directives(s, out.directives);
}

ValidationResult validate_directives(const Scenario& s, const std::vector<Directive>& dirs) {
    if (dirs.size() != s.operator_notes.size()) return {false, "directive count mismatch"};
    for (std::size_t i = 0; i < dirs.size(); ++i) {
        const auto& d = dirs[i];
        if (d.note_index != i) return {false, "note_index order mismatch"};
        for (std::size_t j = 1; j < d.hours.size(); ++j) if (d.hours[j] <= d.hours[j - 1]) return {false, "directive hours must be sorted and unique"};
        for (int h : d.hours) if (h < 0 || h >= static_cast<int>(HOURS)) return {false, "directive hour out of range"};
        if (d.type == DirectiveType::NoOp) {
            if (d.applies) return {false, "no_op must have applies=false"};
        } else {
            if (!d.applies) return {false, "non-no_op directive must have applies=true"};
            if (d.type == DirectiveType::SolarReduction && (d.factor < -EPS || d.factor > 1.0 + EPS)) return {false, "solar factor out of range"};
            if (d.type == DirectiveType::MinimumBatteryReserve && (d.minimum_energy_kwh < -EPS || d.minimum_energy_kwh > s.battery.capacity_kwh + EPS)) return {false, "reserve out of range"};
            if (d.type == DirectiveType::MaxGridWindow && d.max_grid_kwh < -EPS) return {false, "grid cap must be non-negative"};
        }
    }
    return {true, {}};
}

boost::json::value directives_to_json(const InterpretationResult& ir) {
    boost::json::array arr;
    for (const auto& d : ir.directives) {
        boost::json::object x;
        x["note_index"] = static_cast<std::int64_t>(d.note_index);
        x["applies"] = d.applies;
        x["directive_type"] = directive_name(d.type);
        if (!d.applies || d.type == DirectiveType::NoOp) {
            x["structured_adjustment"] = nullptr;
        } else {
            boost::json::object a;
            boost::json::array hs;
            for (int h : d.hours) hs.push_back(h);
            a["hours"] = std::move(hs);
            switch (d.type) {
                case DirectiveType::SolarReduction: a["factor"] = d.factor; break;
                case DirectiveType::MinimumBatteryReserve: a["minimum_energy_kwh"] = d.minimum_energy_kwh; break;
                case DirectiveType::MaxGridWindow: a["max_grid_kwh"] = d.max_grid_kwh; break;
                case DirectiveType::NoChargeWindow:
                case DirectiveType::NoDischargeWindow:
                case DirectiveType::NoOp: break;
            }
            x["structured_adjustment"] = std::move(a);
        }
        x["explanation"] = d.explanation;
        arr.push_back(std::move(x));
    }
    return arr;
}

}