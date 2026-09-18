#include "optimizer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace gridwise {
namespace {

class LPSolver {
public:
    static constexpr double EPS = 1e-9;
    static constexpr double INF = 1e100;

    int m, n;
    std::vector<int> B, N;
    std::vector<std::vector<double>> D;

    LPSolver(const std::vector<std::vector<double>>& A,
             const std::vector<double>& b,
             const std::vector<double>& c)
        : m(static_cast<int>(b.size())), n(static_cast<int>(c.size())),
          B(m), N(n + 1), D(m + 2, std::vector<double>(n + 2)) {
        for (int i = 0; i < m; ++i)
            for (int j = 0; j < n; ++j)
                D[i][j] = A[i][j];
        for (int i = 0; i < m; ++i) { B[i] = n + i; D[i][n] = -1; D[i][n + 1] = b[i]; }
        for (int j = 0; j < n; ++j) { N[j] = j; D[m][j] = -c[j]; }
        N[n] = -1;
        D[m + 1][n] = 1;
    }

    void Pivot(int r, int s) {
        const double inv = 1.0 / D[r][s];
        for (int i = 0; i < m + 2; ++i) if (i != r)
            for (int j = 0; j < n + 2; ++j) if (j != s)
                D[i][j] -= D[r][j] * D[i][s] * inv;
        for (int j = 0; j < n + 2; ++j) if (j != s) D[r][j] *= inv;
        for (int i = 0; i < m + 2; ++i) if (i != r) D[i][s] *= -inv;
        D[r][s] = inv;
        std::swap(B[r], N[s]);
    }

    bool Simplex(int phase) {
        const int x = (phase == 1) ? m + 1 : m;
        for (;;) {
            int s = -1;
            for (int j = 0; j <= n; ++j) {
                if (phase == 2 && N[j] == -1) continue;
                if (s == -1 || D[x][j] < D[x][s] - EPS ||
                    (std::abs(D[x][j] - D[x][s]) <= EPS && N[j] < N[s])) s = j;
            }
            if (D[x][s] >= -EPS) return true;
            int r = -1;
            for (int i = 0; i < m; ++i) {
                if (D[i][s] <= EPS) continue;
                if (r == -1) r = i;
                else {
                    const double lhs = D[i][n + 1] / D[i][s];
                    const double rhs = D[r][n + 1] / D[r][s];
                    if (lhs < rhs - EPS || (std::abs(lhs - rhs) <= EPS && B[i] < B[r])) r = i;
                }
            }
            if (r == -1) return false;
            Pivot(r, s);
        }
    }

    double Solve(std::vector<double>& x) {
        int r = 0;
        for (int i = 1; i < m; ++i) if (D[i][n + 1] < D[r][n + 1]) r = i;
        if (m > 0 && D[r][n + 1] < -EPS) {
            Pivot(r, n);
            if (!Simplex(1) || D[m + 1][n + 1] < -EPS) return -INF;
            if (std::abs(D[m + 1][n + 1]) > EPS) return -INF;
            if (std::find(B.begin(), B.end(), -1) != B.end()) {
                r = static_cast<int>(std::find(B.begin(), B.end(), -1) - B.begin());
                int s = 0;
                for (int j = 1; j <= n; ++j)
                    if (D[r][j] < D[r][s] - EPS || (std::abs(D[r][j] - D[r][s]) <= EPS && N[j] < N[s])) s = j;
                Pivot(r, s);
            }
        }
        if (!Simplex(2)) return INF;
        x.assign(n, 0.0);
        for (int i = 0; i < m; ++i) if (B[i] < n) x[B[i]] = D[i][n + 1];
        return D[m][n + 1];
    }
};

struct ModelData {
    std::vector<double> solar_factor;
    std::vector<double> min_reserve;
    std::vector<bool> no_charge;
    std::vector<bool> no_discharge;
    std::vector<double> max_grid;
};

ModelData aggregate(const Scenario& s, const std::vector<Directive>& dirs) {
    ModelData m;
    m.solar_factor.assign(HOURS, 1.0);
    m.min_reserve.assign(HOURS, s.battery.minimum_energy_kwh);
    m.no_charge.assign(HOURS, false);
    m.no_discharge.assign(HOURS, false);
    m.max_grid.assign(HOURS, std::numeric_limits<double>::infinity());

    for (const auto& d : dirs) {
        if (!d.applies) continue;
        for (int h : d.hours) {
            switch (d.type) {
                case DirectiveType::SolarReduction:
                    m.solar_factor[static_cast<std::size_t>(h)] *= d.factor;
                    break;
                case DirectiveType::MinimumBatteryReserve:
                    m.min_reserve[static_cast<std::size_t>(h)] = std::max(m.min_reserve[static_cast<std::size_t>(h)], d.minimum_energy_kwh);
                    break;
                case DirectiveType::NoChargeWindow:
                    m.no_charge[static_cast<std::size_t>(h)] = true;
                    break;
                case DirectiveType::NoDischargeWindow:
                    m.no_discharge[static_cast<std::size_t>(h)] = true;
                    break;
                case DirectiveType::MaxGridWindow:
                    m.max_grid[static_cast<std::size_t>(h)] = std::min(m.max_grid[static_cast<std::size_t>(h)], d.max_grid_kwh);
                    break;
                case DirectiveType::NoOp:
                    break;
            }
        }
    }
    return m;
}

}

Plan optimize(const Scenario& s, const std::vector<Directive>& directives) {
    const auto vm = aggregate(s, directives);

    constexpr int G = 0;
    constexpr int S = 1 * static_cast<int>(HOURS);
    constexpr int C = 2 * static_cast<int>(HOURS);
    constexpr int D = 3 * static_cast<int>(HOURS);
    constexpr int E = 4 * static_cast<int>(HOURS);
    constexpr int VARS = 5 * static_cast<int>(HOURS);

    auto idx = [](int base, int h) { return base + h; };
    std::vector<std::vector<double>> A;
    std::vector<double> b;
    std::vector<double> objective(VARS, 0.0);

    auto add_le = [&](const std::vector<std::pair<int, double>>& terms, double rhs) {
        std::vector<double> row(VARS, 0.0);
        for (const auto& [j, v] : terms) row[j] += v;
        A.push_back(std::move(row));
        b.push_back(rhs);
    };
    auto add_eq = [&](const std::vector<std::pair<int, double>>& terms, double rhs) {
        add_le(terms, rhs);
        std::vector<std::pair<int, double>> neg;
        neg.reserve(terms.size());
        for (const auto& [j, v] : terms) neg.emplace_back(j, -v);
        add_le(neg, -rhs);
    };

    for (int h = 0; h < static_cast<int>(HOURS); ++h) {
        const int g = idx(G, h), sol = idx(S, h), ch = idx(C, h), dis = idx(D, h), e = idx(E, h);
        const auto& hr = s.hours[static_cast<std::size_t>(h)];

        // Grid + solar + discharge = demand + charge.
        add_eq({{g, 1.0}, {sol, 1.0}, {dis, 1.0}, {ch, -1.0}}, hr.demand_kwh);

        // State transition: E[h] = E[h-1] + charge - discharge.
        if (h == 0) {
            add_eq({{e, 1.0}, {ch, -1.0}, {dis, 1.0}}, s.battery.initial_energy_kwh);
        } else {
            add_eq({{e, 1.0}, {idx(E, h - 1), -1.0}, {ch, -1.0}, {dis, 1.0}}, 0.0);
        }

        add_le({{sol, 1.0}}, hr.solar_kwh * vm.solar_factor[static_cast<std::size_t>(h)]);
        add_le({{ch, 1.0}}, s.battery.max_charge_kwh_per_hour);
        add_le({{dis, 1.0}}, s.battery.max_discharge_kwh_per_hour);
        add_le({{e, 1.0}}, s.battery.capacity_kwh);
        add_le({{e, -1.0}}, -vm.min_reserve[static_cast<std::size_t>(h)]);

        if (vm.no_charge[static_cast<std::size_t>(h)]) add_le({{ch, 1.0}}, 0.0);
        if (vm.no_discharge[static_cast<std::size_t>(h)]) add_le({{dis, 1.0}}, 0.0);
        if (std::isfinite(vm.max_grid[static_cast<std::size_t>(h)])) add_le({{g, 1.0}}, vm.max_grid[static_cast<std::size_t>(h)]);

        // Minimize grid cost => maximize negative cost.
        objective[g] = -hr.tariff_bdt_per_kwh;
    }

    // End-of-day battery neutrality.
    add_eq({{idx(E, 23), 1.0}}, s.battery.initial_energy_kwh);

    LPSolver solver(A, b, objective);
    std::vector<double> x;
    const double optimum = solver.Solve(x);
    if (!std::isfinite(optimum) || optimum <= -LPSolver::INF / 2) throw std::runtime_error("optimization infeasible");
    if (optimum >= LPSolver::INF / 2) throw std::runtime_error("optimization unbounded");

    Plan plan;
    double energy = s.battery.initial_energy_kwh;
    for (int h = 0; h < static_cast<int>(HOURS); ++h) {
        const auto& hr = s.hours[static_cast<std::size_t>(h)];
        double g = x[idx(G, h)];
        double sol = x[idx(S, h)];
        double ch = x[idx(C, h)];
        double dis = x[idx(D, h)];

        auto clean = [](double v) { return (std::abs(v) < 1e-8 || v < 0.0) ? 0.0 : v; };
        g = clean(g); sol = clean(sol); ch = clean(ch); dis = clean(dis);
        const double both = std::min(ch, dis);
        if (both > 0.0) { ch -= both; dis -= both; }

        energy += ch - dis;
        if (energy < 0.0 && energy > -1e-7) energy = 0.0;
        if (std::abs(energy - s.battery.capacity_kwh) < 1e-8) energy = s.battery.capacity_kwh;
        if (std::abs(energy - s.battery.minimum_energy_kwh) < 1e-8) energy = s.battery.minimum_energy_kwh;

        auto& p = plan.hours[static_cast<std::size_t>(h)];
        p.hour = h;
        p.grid_kwh = g;
        p.solar_used_kwh = sol;
        if (ch > 1e-7) { p.battery_action = "charge"; p.battery_kwh = ch; }
        else if (dis > 1e-7) { p.battery_action = "discharge"; p.battery_kwh = dis; }
        else { p.battery_action = "idle"; p.battery_kwh = 0.0; }
        p.battery_energy_after_kwh = energy;

        plan.total_grid_kwh += g;
        plan.total_cost_bdt += g * hr.tariff_bdt_per_kwh;
        plan.peak_grid_kwh = std::max(plan.peak_grid_kwh, g);
    }

    return plan;
}

}