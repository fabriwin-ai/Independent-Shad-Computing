#include <cmath>

#include "isc/opt/optimizer.hpp"
#include "isc_test.hpp"

using namespace isc::opt;

ISC_TEST(optimizer_analytic_matches_finite_difference) {
    EdgeEnergyLoss loss({1.0, 0.5, 0.5}, 1.25);
    for (double w : {-0.5, 0.0, 0.25, 1.0, 2.5}) {
        std::vector<double> ga, gf;
        CHECK(loss.gradient({w}, ga));
        finite_difference_gradient(loss, {w}, 1e-5, gf);
        CHECK_NEAR(ga[0], gf[0], 1e-6);
    }
}

ISC_TEST(optimizer_converges_to_closed_form) {
    // E(w) = 1 + w + 0.5 w^2 = 1.25  ->  w* = -1 + sqrt(1.5)
    EdgeEnergyLoss loss({1.0, 0.5, 0.5}, 1.25);
    ParameterSet p;
    p.add("weight", 0.25, 0.0, 4.0);
    OptimizerSettings s;
    s.learning_rate = 0.5;
    s.max_iterations = 500;
    s.tolerance = 1e-10;
    auto r = gradient_descent(loss, p, s);
    CHECK(r.converged);
    CHECK(r.used_analytic);
    CHECK(r.final_loss < r.initial_loss);
    CHECK_NEAR(p[0].value, -1.0 + std::sqrt(1.5), 1e-5);
}

ISC_TEST(optimizer_respects_bounds) {
    EdgeEnergyLoss loss({1.0, 0.5, 0.5}, 1.25);
    ParameterSet p;
    p.add("weight", 0.8, 0.3, 1.0);
    OptimizerSettings s;
    s.learning_rate = 0.5;
    s.max_iterations = 500;
    gradient_descent(loss, p, s);
    CHECK_NEAR(p[0].value, 0.3, 1e-9);  // unconstrained optimum 0.2247 lies below the bound
}

ISC_TEST(optimizer_zero_iterations_keeps_parameters) {
    EdgeEnergyLoss loss({1.0, 0.5, 0.5}, 1.25);
    ParameterSet p;
    p.add("weight", 0.25, 0.0, 4.0);
    OptimizerSettings s;
    s.max_iterations = 0;
    auto r = gradient_descent(loss, p, s);
    CHECK_EQ(r.iterations, 0u);
    CHECK_NEAR(p[0].value, 0.25, 0.0);
}

namespace {
struct Bowl final : Loss {  // no analytic gradient -> finite-difference fallback
    double evaluate(const std::vector<double>& p) const override {
        return (p[0] - 3.0) * (p[0] - 3.0) + 2.0 * (p[1] + 1.0) * (p[1] + 1.0);
    }
};
}  // namespace

ISC_TEST(optimizer_finite_difference_fallback) {
    Bowl loss;
    ParameterSet p;
    p.add("x", 0.0, -10.0, 10.0);
    p.add("y", 0.0, -10.0, 10.0);
    OptimizerSettings s;
    s.learning_rate = 0.2;
    s.max_iterations = 200;
    s.tolerance = 1e-9;
    auto r = gradient_descent(loss, p, s);
    CHECK(!r.used_analytic);
    CHECK_NEAR(p[0].value, 3.0, 1e-4);
    CHECK_NEAR(p[1].value, -1.0, 1e-4);
}

ISC_TEST(optimizer_is_stable_on_flat_images) {
    EdgeEnergyLoss loss({0.0, 0.0, 0.0}, 1.25);  // flat frame: no gradients at all
    ParameterSet p;
    p.add("weight", 0.25, 0.0, 4.0);
    OptimizerSettings s;
    s.max_iterations = 8;
    auto r = gradient_descent(loss, p, s);
    CHECK(std::isfinite(r.final_loss));
    CHECK_NEAR(p[0].value, 0.25, 1e-12);
}
