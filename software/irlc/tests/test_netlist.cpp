#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdio>
#include <string>

#include <irl/netlist.hpp>

using namespace irl;

namespace {

const char *vertex_kind_name(NetlistVertexKind kind) {
    switch (kind) {
#define VERT_KIND(NAME, UNION_FIELD)                                                               \
    case NetlistVertexKind::NAME:                                                                  \
        return #NAME;
        VERT_KINDS
#undef VERT_KIND
    }
    return "?";
}

const char *edge_kind_name(NetlistEdgeKind kind) {
    switch (kind) {
#define EDGE_KIND(NAME, ASSOCIATED_VERT_KIND, UNION_FIELD)                                         \
    case NetlistEdgeKind::NAME:                                                                    \
        return #NAME;
        EDGE_KINDS
#undef EDGE_KIND
    }
    return "?";
}

// Human-readable walk of the netlist. This is deliberately a plain stdio dump:
// it gives us something to eyeball while the real printing/formatting API is
// still being designed.
void dump(Netlist &nl) {
    auto verts = nl.vertices();
    std::size_t nv = 0;
    std::puts("vertices:");
    for (auto it = verts.first; it != verts.second; ++it) {
        NetlistVertex &v = *it;
        std::printf("  [%zu] %s", nv++, vertex_kind_name(v.kind()));
        if (auto *n = v.get_data_if<NetlistVertexKind::NET>()) {
            std::printf(" netKind=%d", static_cast<int>(*n));
        }
        if (auto *r = v.get_data_if<NetlistVertexKind::R>()) {
            std::printf(" R=%g", *r);
        }
        if (auto *c = v.get_data_if<NetlistVertexKind::C>()) {
            std::printf(" C=%g", *c);
        }
        std::puts("");
    }

    auto edges = nl.edges();
    std::size_t ne = 0;
    std::puts("edges:");
    for (auto it = edges.first; it != edges.second; ++it) {
        NetlistEdge &e = *it;
        std::printf("  [%zu] %s\n", ne++, edge_kind_name(e.kind()));
    }

    std::printf("(%zu vertices, %zu edges)\n", nv, ne);
}

} // namespace

TEST_CASE("Netlist can be built, walked, and printed", "[netlist]") {
    Netlist nl;

    // An RC low-pass: in --R-- out --C-- gnd
    auto *input = nl.new_vertex<NetlistVertexKind::NET>(std::string("in"), NetKind::CIR_INPUT);
    auto *output = nl.new_vertex<NetlistVertexKind::NET>(std::string("out"), NetKind::CIR_OUTPUT);
    auto *gnd = nl.new_vertex<NetlistVertexKind::NET>(std::string("0"), NetKind::V_GND);
    auto *r = nl.new_vertex<NetlistVertexKind::R>(std::string("R1"), 1000.0);
    auto *c = nl.new_vertex<NetlistVertexKind::C>(std::string("C1"), 1e-6);

    nl.connect<NetlistEdgeKind::PIN_R>(r, input, NoData{});
    nl.connect<NetlistEdgeKind::PIN_R>(r, output, NoData{});
    nl.connect<NetlistEdgeKind::PIN_C>(c, output, NoData{});
    nl.connect<NetlistEdgeKind::PIN_C>(c, gnd, NoData{});

    // A topology vertex/edge so both data-carrying directions are exercised.
    auto *xbar = nl.new_vertex<NetlistVertexKind::TOPO_XBAR>(std::string("X1"), 3u);
    auto *topo_net = nl.new_vertex<NetlistVertexKind::NET>(std::string("t0"), NetKind::WIRE);
    auto *xpin = nl.connect<NetlistEdgeKind::PIN_TOPO_XBAR>(
        xbar, topo_net, XbarPinId{.side = XbarPinId::Side::ROW, .index = 7});

    dump(nl);

    std::size_t nv = 0;
    auto verts = nl.vertices();
    for (auto it = verts.first; it != verts.second; ++it) {
        ++nv;
    }

    std::size_t ne = 0;
    auto edges = nl.edges();
    for (auto it = edges.first; it != edges.second; ++it) {
        ++ne;
    }

    REQUIRE(nv == 7);
    REQUIRE(ne == 5);

    SECTION("vertex data is kind-gated") {
        REQUIRE(r->kind() == NetlistVertexKind::R);
        REQUIRE(r->get_data_if<NetlistVertexKind::R>() != nullptr);
        REQUIRE(*r->get_data_if<NetlistVertexKind::R>() == 1000.0);

        // Wrong kind -> nullptr, never a reinterpreted field.
        REQUIRE(r->get_data_if<NetlistVertexKind::C>() == nullptr);
        REQUIRE(r->get_data_if<NetlistVertexKind::NET>() == nullptr);
    }

    SECTION("const vertex data is kind-gated") {
        Netlist const &cnl = nl;
        auto cverts = cnl.vertices();

        NetlistVertex const *const_r = nullptr;
        for (auto it = cverts.first; it != cverts.second; ++it) {
            if (it->kind() == NetlistVertexKind::R) {
                const_r = &*it;
            }
        }

        REQUIRE(const_r != nullptr);
        double const *value = const_r->get_data_if<NetlistVertexKind::R>();
        REQUIRE(value != nullptr);
        REQUIRE(*value == 1000.0);
    }

    SECTION("edge data is kind-gated") {
        auto *pin = xpin->get_data_if<NetlistEdgeKind::PIN_TOPO_XBAR>();
        REQUIRE(pin != nullptr);
        REQUIRE(pin->side == XbarPinId::Side::ROW);
        REQUIRE(pin->index == 7);

        REQUIRE(xpin->get_data_if<NetlistEdgeKind::PIN_R>() == nullptr);
    }
}
