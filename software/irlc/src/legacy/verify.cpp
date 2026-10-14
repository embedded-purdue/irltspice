#include "core/verify.hpp"

#include <boost/graph/graph_concepts.hpp>
#include <variant>

#include "util/macros.hpp"

using namespace std;
using namespace boost;

uint32_t IrlVerifier::check_netlist_violations(Netlist const &netlist) {

    uint32_t violation_count = 0;

    for (auto const &rule : RAW_GRAPH_RULES) {
        auto result = rule.rule(netlist);
        visit(overloads{[&](no_violation_t const &_) {
                            if (compiler.opts.should_verbose_verify()) {
                                compiler.log_fd << "Rule " << rule.rule_name << " PASSED!"
                                                << "\n";
                            }
                        },
                        [&](string msg) {
                            violation_count++;
                            compiler.log_fd << "VIOLATION of graph rule [" << rule.rule_name << "]";
                            if (msg.size() > 0) {
                                compiler.log_fd << ":\n    " << msg << "\n";
                            } else {
                                compiler.log_fd << "\n";
                            }
                        }},
              result);
    }

    pair verts = vertices(netlist);
    for (RawVert v = *verts.first; verts.first != verts.second; verts.first++, v = *verts.first) {
        for (auto const &rule : RAW_VERTEX_RULES) {
            auto result = rule.rule(netlist, v);
            visit(overloads{[&](no_violation_t const &_) {
                                if (compiler.opts.should_verbose_verify()) {
                                    compiler.log_fd << "Rule " << rule.rule_name
                                                    << " PASSED by: " << netlist[v].name << "\n";
                                }
                            },
                            [&](string msg) {
                                violation_count++;
                                compiler.log_fd << "VIOLATION at [" << netlist[v].name
                                                << "] of rule [" << rule.rule_name << "]";
                                if (msg.size() > 0) {
                                    compiler.log_fd << ":\n    " << msg << "\n";
                                } else {
                                    compiler.log_fd << "\n";
                                }
                            }},
                  result);
        }
    }

    return violation_count;
}
