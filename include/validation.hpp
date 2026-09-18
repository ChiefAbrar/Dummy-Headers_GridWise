#pragma once
#include "types.hpp"
#include <string>
#include <vector>
#include <boost/json.hpp>

namespace gridwise {

struct ValidationResult {
    bool ok{false};
    std::string error;
};

ValidationResult parse_scenario(const boost::json::value& v, Scenario& out);
ValidationResult validate_directives(const Scenario& s, const boost::json::value& v, InterpretationResult& out);
ValidationResult validate_directives(const Scenario& s, const std::vector<Directive>& dirs);

boost::json::value directives_to_json(const InterpretationResult& ir);

}