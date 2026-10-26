#include "irl/core/debug.hpp"

using namespace irl;
using enum irl::NetlistVertexKind;

void debug_print_netlist(std::ostream &out, irl::Netlist const &netlist) {
    out << "COMPONENT OVERVIEW:\n";
    auto verts = netlist.vertices();
    for (auto v = verts.first; v != verts.second; v++) {
        NetlistVertex vertex = *v;
        if (vertex.kind() != NET) {
            out << vertex.name() << ", connected to: ";
            for (NetlistEdgeIdx e : vertex.connections()) {
                out << "(" << e.value << ")" << netlist[e].net().name() << ", ";
            }
            out << "\n";
        }
    }
    out << std::endl;
    out << "NET OVERVIEW:\n";
    for (auto v = verts.first; v != verts.second; v++) {
        NetlistVertex vertex = *v;
        std::cout << vertex.idx().value << std::endl;
        auto edges = vertex.connections();
        if (vertex.kind() == NET) {
            if (edges.size() <= 1) {
                out << "Net: " << vertex.name() << ", is unconnected";
            } else {
                out << "Net: " << vertex.name() << ", bridges: ";
                for (NetlistEdgeIdx e : edges) {
                    out << netlist[e].component().name() << ", ";
                }
            }
            out << "\n";
        }
    }
}
