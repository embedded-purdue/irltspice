#include "irl/core/debug.hpp"

using namespace irl;
using enum irl::NetlistVertexKind;

void debug_print_netlist(std::ostream &out, irl::Netlist const &netlist) {
    out << "COMPONENT OVERVIEW:\n";
    std::pair verts = netlist.vertices();
    for (auto v = verts.first; v != verts.second; v++) {
        NetlistVertex const &vertex = *v;
        if (vertex.kind() != NET) {
            out << vertex.name() << ", connected to: ";
            for (auto e : vertex.connections()) {
                out << e->mNet->name() << ", ";
            }
            out << "\n";
        }
    }
    out << std::endl;
    out << "NET OVERVIEW:\n";
    for (auto v = verts.first; v != verts.second; v++) {
        NetlistVertex const &vertex = *v;
        auto edges = v->connections();
        if (vertex.kind() == NET) {
            if (edges.size() <= 1) {
                out << "Net: " << vertex.name() << ", is unconnected";
            } else {
                out << "Net: " << vertex.name() << ", bridges: ";
                for (auto e : edges) {
                    out << e->mComponent->name() << ", ";
                }
            }
            out << "\n";
        }
    }
}
