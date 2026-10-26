#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <irl/netlist.hpp>

using namespace irl;

namespace {

// Human-readable walk of the netlist. This is deliberately a plain stdio dump:
// it gives us something to eyeball while the real printing/formatting API is
// still being designed.
void dump(Netlist &nl) {
    auto verts = nl.vertices();
    std::size_t nv = 0;
    std::puts("vertices:");
    for (auto it = verts.first; it != verts.second; ++it) {
        NetlistVertex v = *it;
        std::printf("  [%zu] %s", nv++, VertKindName(v.kind()).begin());
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
        NetlistEdge e = *it;
        std::printf("  [%zu] %s\n", ne++, EdgeKindName(e.kind()).begin());
    }

    std::printf("(%zu vertices, %zu edges)\n", nv, ne);
}

} // namespace

TEST_CASE("Netlist can be built, walked, and printed", "[netlist]") {
    Netlist nl;

    // An RC low-pass: in --R-- out --C-- gnd
    auto input =
        nl.new_vertex<NetlistVertexKind::NET_SPECIAL>(std::string("IRLS_CIRCUIT_INPUT"), NoData{});
    auto output =
        nl.new_vertex<NetlistVertexKind::NET_SPECIAL>(std::string("IRLS_CIRCUIT_OUTPUT"), NoData{});
    auto gnd = nl.new_vertex<NetlistVertexKind::NET_V_GND>(std::string("GND"), NoData{});
    auto r = nl.new_vertex<NetlistVertexKind::R>(std::string("R1"), 1000.0);
    auto c = nl.new_vertex<NetlistVertexKind::C>(std::string("C1"), 1e-6);

    nl.connect<NetlistEdgeKind::PIN_R>(r, input, NoData{});
    nl.connect<NetlistEdgeKind::PIN_R>(r, output, NoData{});
    nl.connect<NetlistEdgeKind::PIN_C>(c, output, NoData{});
    nl.connect<NetlistEdgeKind::PIN_C>(c, gnd, NoData{});

    // A topology vertex/edge so both data-carrying directions are exercised.
    auto xbar = nl.new_vertex<NetlistVertexKind::TOPO_XBAR>(std::string("X1"), {});
    auto topo_net = nl.new_vertex<NetlistVertexKind::NET>(std::string("t0"), NoData{});
    auto xpin = nl.connect<NetlistEdgeKind::PIN_TOPO_XBAR>(
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
        REQUIRE(nl[r].kind() == NetlistVertexKind::R);
        REQUIRE(nl[r].get_data_if<NetlistVertexKind::R>() != nullptr);
        REQUIRE(*nl[r].get_data_if<NetlistVertexKind::R>() == 1000.0);

        // Wrong kind -> nullptr, never a reinterpreted field.
        REQUIRE(nl[r].get_data_if<NetlistVertexKind::C>() == nullptr);
        REQUIRE(nl[r].get_data_if<NetlistVertexKind::NET>() == nullptr);
    }

    SECTION("const vertex data is kind-gated") {
        Netlist const &cnl = nl;
        auto cverts = cnl.vertices();

        NetlistVertexIdx const_r{};
        for (auto it = cverts.first; it != cverts.second; ++it) {
            if ((*it).kind() == NetlistVertexKind::R) {
                const_r = (*it).idx();
            }
        }

        REQUIRE(!const_r.is_null());
        double const *value = cnl[const_r].get_data_if<NetlistVertexKind::R>();
        REQUIRE(value != nullptr);
        REQUIRE(*value == 1000.0);
    }

    SECTION("edge data is kind-gated") {
        auto *pin = nl[xpin].get_data_if<NetlistEdgeKind::PIN_TOPO_XBAR>();
        REQUIRE(pin != nullptr);
        REQUIRE(pin->side == XbarPinId::Side::ROW);
        REQUIRE(pin->index == 7);

        REQUIRE(nl[xpin].get_data_if<NetlistEdgeKind::PIN_R>() == nullptr);
    }

    SECTION("const-correctness is enforced by the handle type") {
        using MutableData = decltype(std::declval<Netlist &>()[NetlistVertexIdx{}]
                                         .get_data_if<NetlistVertexKind::R>());
        using ConstData = decltype(std::declval<Netlist const &>()[NetlistVertexIdx{}]
                                       .get_data_if<NetlistVertexKind::R>());
        static_assert(std::is_same_v<MutableData, double *>);
        static_assert(std::is_same_v<ConstData, double const *>);
        REQUIRE(true);
    }
}

TEST_CASE("Removed vertices are tombstoned and skipped by iteration", "[netlist]") {
    Netlist nl;
    auto a = nl.new_vertex<NetlistVertexKind::NET_SPECIAL>(std::string("A"), NoData{});
    auto b = nl.new_vertex<NetlistVertexKind::NET_V_GND>(std::string("B"), NoData{});
    auto r = nl.new_vertex<NetlistVertexKind::R>(std::string("R"), 1.0);
    nl.connect<NetlistEdgeKind::PIN_R>(r, a, NoData{});
    nl.connect<NetlistEdgeKind::PIN_R>(r, b, NoData{});

    REQUIRE(nl[r].connections().size() == 2);

    nl.remove(b);

    REQUIRE(!nl[b].is_valid());
    REQUIRE(nl[r].connections().size() == 1);

    std::size_t live_verts = 0;
    auto verts = nl.vertices();
    for (auto it = verts.first; it != verts.second; ++it) {
        ++live_verts;
    }
    REQUIRE(live_verts == 2);

    std::size_t live_edges = 0;
    auto edges = nl.edges();
    for (auto it = edges.first; it != edges.second; ++it) {
        ++live_edges;
    }
    REQUIRE(live_edges == 1);
}

TEST_CASE("Removed slots are reused once the pool is at capacity", "[netlist]") {
    Netlist nl;
    nl.reserve_vertices(20);

    std::vector<NetlistVertexIdx> ids;
    for (int i = 0; i < 20; ++i) {
        std::ostringstream name;
        name << "n" << i;
        ids.push_back(nl.new_vertex<NetlistVertexKind::NET>(name.str(), NoData{}));
    }

    NetlistVertexIdx removed = ids[5];
    nl.remove(removed);
    REQUIRE(!nl[removed].is_valid());

    NetlistVertexIdx reused = nl.new_vertex<NetlistVertexKind::NET>(std::string("new"), NoData{});
    REQUIRE(reused == removed);
    REQUIRE(nl[reused].name() == "new");
}
