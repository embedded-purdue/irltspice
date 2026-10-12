#pragma once

#include <boost/graph/filtered_graph.hpp>
#include <boost/graph/graph_concepts.hpp>
#include <cstdint>
#include <cstring>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

#include "core/board_info.hpp"
#include "core/compiler.hpp"
#include "core/netlist.hpp"
#include "util/boost_util.hpp"
#include "util/macros.hpp"

typedef enum no_violation_t { NO_VIOLATION } no_violation_t;

typedef std::variant<no_violation_t, std::string> RuleViolationResult;

typedef RuleViolationResult (*VertexRuleViolates_t)(Netlist const &netlist, NetlistVertex const *v);

typedef RuleViolationResult (*GraphRuleViolates_t)(Netlist const &netlist);

struct RawNetlistVertexRule {
    const char *rule_name;
    VertexRuleViolates_t rule;
};

struct RawNetlistGraphRule {
    const char *rule_name;
    GraphRuleViolates_t rule;
};

typedef std::variant<const RawNetlistGraphRule *, const RawNetlistVertexRule *> RawNetlistRuleRef;

class IrlVerifier {
    IrlCompiler const &compiler;

  public:
    IrlVerifier(IrlCompiler const &compiler) : compiler(compiler) {};

    // Returns the number of rule violations logged
    uint32_t check_netlist_violations(Netlist const &netlist);
};

constexpr std::array RAW_GRAPH_RULES = {
    RawNetlistGraphRule{.rule_name = "Circuit contains output",
                        .rule = [](Netlist const &netlist) -> RuleViolationResult {
                            for (auto v : pair_to_iter(netlist.vertices())) {
                                if (netlist[v].kind == NET &&
                                    netlist[v].value.net_value == CIR_OUTPUT) {
                                    return NO_VIOLATION;
                                }
                            }
                            return "";
                        }},
    RawNetlistGraphRule{.rule_name = "Circuit contains input",
                        .rule = [](Netlist const &netlist) -> RuleViolationResult {
                            for (auto v : pair_to_iter(boost::vertices(netlist))) {
                                if (netlist[v].kind == NET &&
                                    netlist[v].value.net_value == CIR_INPUT) {
                                    return NO_VIOLATION;
                                }
                            }
                            return "";
                        }},
};

constexpr std::array RAW_VERTEX_RULES = {
    RawNetlistVertexRule{
        .rule_name = "Opamp supply plus must be V_HIGH or unconnected",
        .rule = [](Netlist const &netlist, NetlistVertex const *v) -> RuleViolationResult {
            if (netlist[v].kind != OPAMP)
                return NO_VIOLATION;

            for (auto e : pair_to_iter(boost::out_edges(v, netlist))) {

                RawNetlistVertexInfo const &target_info = netlist[boost::target(e, netlist)];
                assert(target_info.kind == NET);

                if (netlist[e].kind == PIN_OPAMP_SUPPLY_PLUS) {
                    if (target_info.value.net_value != V_HIGH) {
                        return "";
                    }
                }
            }

            return NO_VIOLATION;
        },
    },
    RawNetlistVertexRule{
        .rule_name = "Opamp supply minus must be V_NEG or unconnected",
        .rule = [](Netlist const &netlist, NetlistVertex const *v) -> RuleViolationResult {
            if (netlist[v].kind != OPAMP)
                return NO_VIOLATION;

            for (auto e : pair_to_iter(boost::out_edges(v, netlist))) {

                RawNetlistVertexInfo const &target_info = netlist[boost::target(e, netlist)];
                assert(target_info.kind == NET);

                if (netlist[e].kind == PIN_OPAMP_SUPPLY_MINUS) {
                    if (target_info.value.net_value != V_NEG) {
                        return "";
                    }
                }
            }

            return NO_VIOLATION;
        },
    },
    RawNetlistVertexRule{
        .rule_name = "Opamp output must not drive a voltage",
        .rule = [](Netlist const &netlist, NetlistVertex const *v) -> RuleViolationResult {
            if (netlist[v].kind != OPAMP)
                return NO_VIOLATION;

            for (auto e : pair_to_iter(boost::out_edges(v, netlist))) {

                RawNetlistVertexInfo const &target_info = netlist[boost::target(e, netlist)];
                assert(target_info.kind == NET);

                if (netlist[e].kind == PIN_OPAMP_OUT) {
                    if (target_info.value.net_value == V_NEG ||
                        target_info.value.net_value == V_HIGH ||
                        target_info.value.net_value == V_GND) {
                        return "";
                    }
                }
            }

            return NO_VIOLATION;
        },
    },
    RawNetlistVertexRule{
        .rule_name = "Components should not be bypassed",
        .rule = [](Netlist const &netlist, NetlistVertex const *v) -> RuleViolationResult {
            if (netlist[v].kind == R || netlist[v].kind == C || netlist[v].kind == DIODE ||
                netlist[v].kind == CELL_BUFFER) {
            } else {
                return NO_VIOLATION;
            }

            if (boost::out_degree(v, netlist) != 2) {
                return "Component must be connected to 2 nets";
            }

            std::pair edges = boost::out_edges(v, netlist);
            auto e1 = *edges.first;
            auto e2 = *(edges.first + 1);

            auto t1 = boost::target(e1, netlist);
            auto t2 = boost::target(e2, netlist);

            if (t1 == t2) {
                return "Component bypassed by net: " + netlist[t1].name;
            }

            return NO_VIOLATION;
        },
    },
    RawNetlistVertexRule{
        .rule_name = "Circuit output must be driven by only a buffer",
        .rule = [](Netlist const &netlist, NetlistVertex const *v) -> RuleViolationResult {
            if (netlist[v].kind != NET || netlist[v].value.net_value != CIR_OUTPUT) {
                return NO_VIOLATION;
            }

            if (boost::in_degree(v, netlist) > 1) {
                std::string out = "Output driven by multiple nets: [";
                auto iter = pair_to_iter(boost::in_edges(v, netlist));
                out += "\"";
                out += netlist[boost::source(*iter.begin(), netlist)].name;
                out += "\"";
                iter.advance(1);
                for (auto e : iter) {
                    out += "\", ";
                    out += netlist[boost::source(e, netlist)].name;
                    out += "\"";
                }
                out += "]";
                return out;
            }

            if (netlist[v].kind == NET && netlist[v].value.net_value == CIR_OUTPUT) {
                auto e = *boost::in_edges(v, netlist).first;
                auto const &edge = netlist[e];
                auto const &src = netlist[boost::source(e, netlist)];
                if (src.kind != CELL_BUFFER) {
                    return "Output driven by non-cell-buffer: " + src.name;
                } else if (edge.kind != PIN_CELL_BUFFER_OUT) {
                    return "Output driven by input of cell buffer instead of output: " + src.name;
                }
            }

            return NO_VIOLATION;
        },
    },
    RawNetlistVertexRule{
        .rule_name = "Buffer must not drive voltage",
        .rule = [](Netlist const &netlist, NetlistVertex const *v) -> RuleViolationResult {
            if (netlist[v].kind != CELL_BUFFER)
                return NO_VIOLATION;

            for (auto e : pair_to_iter(boost::out_edges(v, netlist))) {

                RawNetlistVertexInfo const &target_info = netlist[boost::target(e, netlist)];
                assert(target_info.kind == NET);

                if (netlist[e].kind == PIN_CELL_BUFFER_OUT) {
                    if (target_info.value.net_value == V_GND ||
                        target_info.value.net_value == V_HIGH ||
                        target_info.value.net_value == V_NEG ||
                        target_info.value.net_value == CIR_INPUT) {
                        return "";
                    }
                }
            }

            return NO_VIOLATION;
        },
    },
    RawNetlistVertexRule{
        .rule_name = "Capacitors must be supported values",
        .rule = [](Netlist const &netlist, NetlistVertex const *v) -> RuleViolationResult {
            if (netlist[v].kind != C)
                return NO_VIOLATION;

            if (!MAIN_BOARD.valid_caps.contains(netlist[v].value.numeric_value)) {
                auto o = std::ostringstream() << "Value must be one of: ";
                auto i = MAIN_BOARD.valid_caps.begin();
                o << *i;
                i++;
                for (; i != MAIN_BOARD.valid_caps.end(); i++) {
                    o << ", " << *i;
                }
                return o.str();
            }

            return NO_VIOLATION;
        },
    },
};

consteval bool rule_exists(const char *rule_name) {
    RawNetlistRuleRef value;
    bool found_value = false;
    for (RawNetlistVertexRule const &vert_rule : RAW_VERTEX_RULES) {
        if (std::string(vert_rule.rule_name).compare(rule_name) == 0) {
            return true;
        }
    }
    for (RawNetlistGraphRule const &graph_rule : RAW_GRAPH_RULES) {
        if (std::string(graph_rule.rule_name).compare(rule_name) == 0) {
            return true;
        }
    }

    throw std::string("No rule matching name ") + rule_name;
}

// Asserts that a rule has been violated, so somehow the rule checker has failed or a rule was
// improperly written.
template <StringLiteral rule_name> void rule_failed() {
    constexpr bool real_rule = rule_exists(rule_name.value);
    if (real_rule) {
        throw std::runtime_error(std::string("An assupmtion protected by rule \"") +
                                 rule_name.value +
                                 "\" has been violated, indicating a problem with irlc's "
                                 "implementation. Please report this bug");
    }
}
