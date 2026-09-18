#pragma once
#include "types.hpp"
#include <vector>

namespace gridwise {

Plan optimize(const Scenario& scenario, const std::vector<Directive>& directives);

}