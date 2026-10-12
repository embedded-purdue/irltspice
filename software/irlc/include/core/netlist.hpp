#pragma once

// Master docs:
// https://www.boost.org/doc/libs/latest/libs/graph/doc/table_of_contents.html

#include <array>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_selectors.hpp>
#include <cstddef>
#include <span>

#include "irl_netlist.hpp"
#include "util/macros.hpp"

// Netlists are represented as a bipartite graph, where the left side
//   consists of vertecies representing nets and the right side consists
//   of vertecies representing components.
//   Each edge has a type corresponding to which pin of the component
//     it is.
//   Each vertex contains metadata about the component/net, i.e.
//     if it is GND/5V or an OpAmp or what resistance it should be

namespace irl {

using enum NetKind;
using enum NetlistVertexKind;
using enum NetlistEdgeKind;

constexpr const std::array IRL_CIR_INPUT_NET_NAMES = {"INPUT"};
constexpr const std::array IRL_CIR_OUTPUT_NET_NAMES = {"OUTPUT"};
constexpr const std::array IRL_V_GND_NET_NAMES = {"0", "GND"};
constexpr const std::array IRL_V_HIGH_NET_NAMES = {"VCC", "VDD", "+5V"};
constexpr const std::array IRL_V_NEG_NET_NAMES = {"-5V"};

#define NET_PAIR(KIND)                                                                             \
    std::pair { std::span(IRL_##KIND##_NET_NAMES.begin(), IRL_##KIND##_NET_NAMES.end()), KIND }

constexpr const std::array RECOGNIZED_NETS = arrify(
    NET_PAIR(CIR_INPUT), NET_PAIR(CIR_OUTPUT), NET_PAIR(V_GND), NET_PAIR(V_HIGH), NET_PAIR(V_NEG));

static inline const char *net_name(NetKind const &netval) {
    switch (netval) {
    case WIRE:
        return "Normal Wire";
    case CIR_INPUT:
        return "Circuit Input";
    case CIR_OUTPUT:
        return "Circuit Output";
    case V_GND:
        return "Ground";
    case V_HIGH:
        return "Positive Voltage (5V)";
    case V_NEG:
        return "Negative Voltage (-5V)";
    }
}

static inline NetKind get_net_kind(auto &net_name) {
    for (auto opt : RECOGNIZED_NETS) {
        if (std::find(opt.first.begin(), opt.first.end(), net_name) != opt.first.end()) {
            return opt.second;
        }
    }
    return WIRE;
};

typedef struct _RecognizedComponent {
    // Identifying info
    const char *long_name;
    NetlistVertexKind component_kind;
    bool numeric_value;

    std::span<const char *const> spice_prefixes;
    std::span<const char *const> spice_values;

    // Ordered according to how they appear in a spice file
    std::span<const NetlistEdgeKind> pins;
} RecognizedComponent;

// Bullshit for listing parseable components
// =========================================
// I really should not have done this, but I realllly wanted it comptime w/o manually naming the
// arrays. Static members of constexpr lambdas being c++23 sucks :(

#define COMPONENT(KIND, NAME, NUMERIC, PFXS, VALS, PINS)                                           \
    static constexpr auto _##KIND##_pfxs = PFXS;                                                   \
    static constexpr auto _##KIND##_vals = VALS;                                                   \
    static constexpr auto _##KIND##_pins = PINS;                                                   \
    static constexpr RecognizedComponent component_##KIND = {                                      \
        .long_name = NAME,                                                                         \
        .component_kind = KIND,                                                                    \
        .numeric_value = NUMERIC,                                                                  \
        .spice_prefixes = _##KIND##_pfxs,                                                          \
        .spice_values = _##KIND##_vals,                                                            \
        .pins = _##KIND##_pins,                                                                    \
    };

struct RCStorage {
    COMPONENT(R, "Resistor", true, arrify("R"), arrify<const char *>(), arrify(PIN_R, PIN_R))
    COMPONENT(C, "Capacitor", true, arrify("C"), arrify<const char *>(), arrify(PIN_C, PIN_C))
    COMPONENT(OPAMP, "Operational Amplifier", false, arrify("U", "XU"),
              arrify("opamp", "kicad_builtin_opamp"),
              arrify(PIN_OPAMP_PLUS, PIN_OPAMP_MINUS, PIN_OPAMP_SUPPLY_PLUS, PIN_OPAMP_SUPPLY_MINUS,
                     PIN_OPAMP_OUT))
    COMPONENT(DIODE, "Diode", false, arrify("D"), arrify<const char *>(),
              arrify(PIN_DIODE_A, PIN_DIODE_K))
    COMPONENT(Q_PNP, "BJT (PNP)", false, arrify("Q"), arrify("PNP"),
              arrify(PIN_Q_PNP_COLLECTOR, PIN_Q_PNP_BASE, PIN_Q_PNP_EMITTER))
    COMPONENT(Q_NPN, "BJT (NPN)", false, arrify("Q"), arrify("NPN"),
              arrify(PIN_Q_NPN_COLLECTOR, PIN_Q_NPN_BASE, PIN_Q_NPN_EMITTER))
    COMPONENT(CUSTOM, "User's custom component", false, arrify("U"), arrify("CUSTOM"),
              arrify(PIN_CUSTOM, PIN_CUSTOM))
    // COMPONENT(CELL_BUFFER, "IRLTSPICE standard cell output buffer", false, arrify("U"),
    //           arrify("IRL_BUFFER"), arrify(PIN_CELL_BUFFER_OUT, PIN_CELL_BUFFER_IN))
};

constexpr RecognizedComponent RECOGNIZED_COMPONENTS[] = {
    RCStorage::component_R,     RCStorage::component_C,     RCStorage::component_OPAMP,
    RCStorage::component_DIODE, RCStorage::component_Q_PNP, RCStorage::component_Q_NPN,
    RCStorage::component_CUSTOM};

} // namespace irl
