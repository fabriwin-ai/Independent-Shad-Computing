#include <sstream>

#include "isc/ir/ir.hpp"

namespace isc::ir {

std::string dump(const Module& m) {
    std::ostringstream s;
    s << "module\n";
    s << "  numerics : precision=" << to_string(m.numerics.precision)
      << " accumulation=" << to_string(m.numerics.accumulation) << (m.numerics.declared ? "" : " (default)") << '\n';
    for (std::size_t i = 0; i < m.artifacts.size(); ++i) {
        const Artifact& a = m.artifacts[i];
        s << "  artifact[" << i << "] " << a.name << " : source=" << m.resource_name(a.source)
          << " gradient=" << (a.gradient ? "enabled" : "disabled") << " iterations=" << a.iterations
          << " weight=" << a.weight << " lr=" << a.learning_rate << " target=" << a.target << " bounds=["
          << a.weight_min << ',' << a.weight_max << "] radius=" << a.radius << '\n';
    }
    s << "  render   : input=" << m.resource_name(m.render.input) << " result=" << m.resource_name(m.render.result)
      << " output=" << m.render.output
      << " backend=" << to_string(m.render.backend) << " profile=" << m.render.profile_name << '\n';
    s << "  order    :";
    if (m.order.empty()) s << " (bypass)";
    for (auto i : m.order) s << ' ' << m.artifacts[i].name;
    s << '\n';
    s << describe(m.profile);
    return s.str();
}

}  // namespace isc::ir
