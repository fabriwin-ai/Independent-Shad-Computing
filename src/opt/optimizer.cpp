#include "isc/opt/optimizer.hpp"

#include <algorithm>
#include <cmath>

namespace isc::opt {

std::size_t ParameterSet::add(std::string name, double value, double min, double max) {
    params_.push_back(Parameter{std::move(name), std::clamp(value, min, max), min, max});
    return params_.size() - 1;
}

std::vector<double> ParameterSet::values() const {
    std::vector<double> v(params_.size());
    for (std::size_t i = 0; i < params_.size(); ++i) v[i] = params_[i].value;
    return v;
}

void ParameterSet::set_values(const std::vector<double>& v) {
    for (std::size_t i = 0; i < params_.size() && i < v.size(); ++i)
        params_[i].value = std::clamp(v[i], params_[i].min, params_[i].max);
}

void finite_difference_gradient(const Loss& loss, const std::vector<double>& p, double step, std::vector<double>& g) {
    g.assign(p.size(), 0.0);
    std::vector<double> q = p;
    for (std::size_t i = 0; i < p.size(); ++i) {
        q[i] = p[i] + step;
        const double hi = loss.evaluate(q);
        q[i] = p[i] - step;
        const double lo = loss.evaluate(q);
        q[i] = p[i];
        g[i] = (hi - lo) / (2.0 * step);
    }
}

OptimizerResult gradient_descent(const Loss& loss, ParameterSet& params, const OptimizerSettings& s) {
    OptimizerResult r;
    std::vector<double> p = params.values();
    std::vector<double> g;
    r.initial_loss = loss.evaluate(p);

    for (std::uint32_t it = 0; it < s.max_iterations;) {
        bool analytic = false;
        if (s.mode != GradientMode::FiniteDifference) analytic = loss.gradient(p, g);
        if (!analytic) finite_difference_gradient(loss, p, s.fd_step, g);
        r.used_analytic = analytic;

        double max_step = 0.0;
        for (std::size_t i = 0; i < p.size(); ++i) {
            const double next = std::clamp(p[i] - s.learning_rate * g[i], params[i].min, params[i].max);
            max_step = std::max(max_step, std::fabs(next - p[i]));
            p[i] = next;
        }
        ++it;
        r.iterations = it;
        if (max_step < s.tolerance) {
            r.converged = true;
            break;
        }
    }
    params.set_values(p);
    r.final_loss = loss.evaluate(p);
    return r;
}

double EdgeEnergyLoss::norm() const { return 1.0 / std::max(e_.A * e_.A, 1e-12); }

double EdgeEnergyLoss::evaluate(const std::vector<double>& p) const {
    const double diff = energy(p.at(0)) - target_ * e_.A;
    return diff * diff * norm();
}

bool EdgeEnergyLoss::gradient(const std::vector<double>& p, std::vector<double>& g) const {
    const double w = p.at(0);
    const double diff = energy(w) - target_ * e_.A;
    g.assign(1, 2.0 * diff * (2.0 * e_.B + 2.0 * w * e_.C) * norm());
    return true;
}

}  // namespace isc::opt
