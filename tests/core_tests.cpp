#include "optimizer.hpp"
#include "replay.hpp"
#include "validation.hpp"
#include <boost/json.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace gridwise;

static boost::json::value load_json(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open fixture: " + path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    boost::system::error_code ec;
    auto v = boost::json::parse(s, ec);
    if (ec) throw std::runtime_error("invalid fixture JSON");
    return v;
}

int main(int argc, char** argv) {
    const std::string fixture = argc > 1 ? argv[1] : "tests/fixtures_public_samples.json";
    const auto root = load_json(fixture);
    const auto& cases = root.as_object().at("cases").as_array();
    int passed = 0;

    for (const auto& cv : cases) {
        const auto& c = cv.as_object();
        Scenario s;
        auto iv = parse_scenario(c.at("input"), s);
        if (!iv.ok) throw std::runtime_error("input validation failed for public case");

        boost::json::object wrapped;
        wrapped["interpretations"] = c.at("expected_output").as_object().at("directive_interpretation");
        InterpretationResult ir;
        auto dv = validate_directives(s, wrapped, ir);
        if (!dv.ok) throw std::runtime_error("reference directives failed validation for " + s.scenario_id);

        const Plan p = optimize(s, ir.directives);
        const auto rv = replay_and_validate(s, ir.directives, p);
        if (!rv.ok) throw std::runtime_error("replay failed for " + s.scenario_id + ": " + rv.error);

        const auto& ev = c.at("expected_output").as_object().at("total_cost_bdt");
        const double expected = ev.is_double() ? ev.as_double() : (ev.is_int64() ? static_cast<double>(ev.as_int64()) : static_cast<double>(ev.as_uint64()));
        if (std::abs(p.total_cost_bdt - expected) > CHECK_TOL) {
            throw std::runtime_error("cost mismatch for " + s.scenario_id + ": got " + std::to_string(p.total_cost_bdt) + " expected " + std::to_string(expected));
        }
        ++passed;
        std::cout << "PASS " << s.scenario_id << " cost=" << p.total_cost_bdt << '\n';
    }

    // Guardrail checks.
    Scenario s;
    auto iv = parse_scenario(cases[0].as_object().at("input"), s);
    if (!iv.ok) return 2;
    boost::json::object bad;
    bad["interpretations"] = boost::json::array{
        boost::json::object{
            {"note_index", 0}, {"applies", false}, {"directive_type", "solar_reduction"},
            {"structured_adjustment", nullptr}, {"explanation", "bad"}
        },
        boost::json::object{
            {"note_index", 1}, {"applies", false}, {"directive_type", "no_op"},
            {"structured_adjustment", nullptr}, {"explanation", "ok"}
        }
    };
    InterpretationResult dummy;
    if (validate_directives(s, bad, dummy).ok) throw std::runtime_error("invalid applies semantics accepted");

    std::cout << "ALL PUBLIC CASES PASS: " << passed << "/" << cases.size() << '\n';
    return passed == static_cast<int>(cases.size()) ? 0 : 1;
}