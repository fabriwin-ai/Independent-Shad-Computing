#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace isc::opt {

// Parameter optimisation (README: "gradient descent is treated as parameter
// optimisation rather than backpropagation through the host engine").
//
//   parameters -> forward -> loss -> gradient -> update -> repeat

struct Parameter {
    std::string name;
    double value = 0.0;
    double min = -1e30;
    double max = 1e30;
};

class ParameterSet {
public:
    std::size_t add(std::string name, double value, double min, double max);
    std::size_t size() const { return params_.size(); }
    Parameter& operator[](std::size_t i) { return params_[i]; }
    const Parameter& operator[](std::size_t i) const { return params_[i]; }
    std::vector<double> values() const;
    void set_values(const std::vector<double>& v);  // clamps to bounds

private:
    std::vector<Parameter> params_;
};

class Loss {
public:
    virtual ~Loss() = default;
    virtual double evaluate(const std::vector<double>& p) const = 0;
    // Analytic gradient. Return false when unavailable -> finite differences.
    virtual bool gradient(const std::vector<double>& p, std::vector<double>& g) const {
        (void)p;
        (void)g;
        return false;
    }
};

enum class GradientMode { Auto, Analytic, FiniteDifference };

struct OptimizerSettings {
    double learning_rate = 0.25;
    std::uint32_t max_iterations = 4;
    double tolerance = 1e-6;  // stop when the largest parameter step is below this
    double fd_step = 1e-4;    // central-difference step
    GradientMode mode = GradientMode::Auto;
};

struct OptimizerResult {
    std::uint32_t iterations = 0;
    double initial_loss = 0.0;
    double final_loss = 0.0;
    bool converged = false;
    bool used_analytic = false;
};

void finite_difference_gradient(const Loss& loss, const std::vector<double>& p, double step, std::vector<double>& g);

// Projected gradient descent: p <- clamp(p - lr * dL/dp, min, max).
OptimizerResult gradient_descent(const Loss& loss, ParameterSet& params, const OptimizerSettings& settings);

// ---------------------------------------------------------------------------
// Reference loss for the detail-enhancement artifact.
//
// With out(w) = in + w * d, the mean squared gradient magnitude ("edge
// energy") of the output is exactly quadratic in w:
//
//     E(w) = A + 2 w B + w^2 C
//     A = mean |grad in|^2,  B = mean(grad in . grad d),  C = mean |grad d|^2
//
// The loss drives E(w) towards target * A, normalised by A^2 so the learning
// rate is independent of image content:
//
//     L(w) = (E(w) - target * A)^2 / max(A^2, eps)
//
// A, B, C are computed once per frame (one reduction pass); the iterations
// then cost O(1). Clamping to [0,1] in the composite is ignored by the model.
// ---------------------------------------------------------------------------
struct EdgeEnergy {
    double A = 0.0;
    double B = 0.0;
    double C = 0.0;
};

class EdgeEnergyLoss final : public Loss {
public:
    EdgeEnergyLoss(EdgeEnergy e, double target) : e_(e), target_(target) {}
    double energy(double w) const { return e_.A + 2.0 * w * e_.B + w * w * e_.C; }
    double evaluate(const std::vector<double>& p) const override;
    bool gradient(const std::vector<double>& p, std::vector<double>& g) const override;

private:
    double norm() const;
    EdgeEnergy e_;
    double target_;
};

}  // namespace isc::opt
