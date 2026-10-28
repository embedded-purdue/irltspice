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
    // ensure_* rounds up to at least MINIMUM_VERT_AMT (32); request exactly that
    // so the pool is full and the free list is consulted on the next insert.
    nl.ensure_vert_capacity(32);

    std::vector<NetlistVertexIdx> ids;
    for (int i = 0; i < 32; ++i) {
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

TEST_CASE("Name map tracks create, rename, remove, and move", "[netlist]") {
    Netlist nl;
    auto a = nl.new_vertex<NetlistVertexKind::NET>(std::string("a"), NoData{});
    auto r = nl.new_vertex<NetlistVertexKind::R>(std::string("R1"), 10.0);

    REQUIRE(*nl.get_vertex("a") == a);
    REQUIRE(*nl.get_vertex("R1") == r);
    REQUIRE_FALSE(nl.get_vertex("missing").has_value());

    // Rename through the handle, including the same-name no-op.
    nl[a].rename("b");
    REQUIRE_FALSE(nl.get_vertex("a").has_value());
    REQUIRE(*nl.get_vertex("b") == a);
    REQUIRE(nl[a].name() == "b");
    nl[a].rename("b");
    REQUIRE(*nl.get_vertex("b") == a);

    // Removal leaves no stale name behind; the old name can be reused.
    nl.remove(a);
    REQUIRE_FALSE(nl.get_vertex("b").has_value());
    auto b = nl.new_vertex<NetlistVertexKind::NET>(std::string("b"), NoData{});
    REQUIRE(*nl.get_vertex("b") == b);

    // Moving must rebind the name map to the destination's storage.
    Netlist moved = std::move(nl);
    REQUIRE(*moved.get_vertex("b") == b);
    REQUIRE(*moved.get_vertex("R1") == r);
    REQUIRE_FALSE(nl.get_vertex("b").has_value());
    REQUIRE_FALSE(nl.get_vertex("R1").has_value());
}

TEST_CASE("Vertex growth relocates live data and preserves the free list", "[netlist]") {
    // ensure_* has a floor of MINIMUM_VERT_AMT (32); use multiples of that floor
    // so the pool is exactly full when we expect the free list to be consulted.
    constexpr int kBaseCap = 32;
    constexpr int kGrownCap = 64;

    Netlist nl;
    nl.ensure_vert_capacity(kBaseCap);

    std::vector<std::string> names;
    std::vector<NetlistVertexIdx> ids;
    for (int i = 0; i < 4; ++i) {
        // Longer than SmallStr's inline capacity, so this exercises the heap path.
        names.push_back("a_vertex_name_that_is_longer_than_inline_" + std::to_string(i));
        ids.push_back(nl.new_vertex<NetlistVertexKind::NET>(names.back(), NoData{}));
    }

    nl.remove(ids[1]);
    nl.remove(ids[2]);

    // Force growth with tombstones present.
    nl.ensure_vert_capacity(kGrownCap);

    REQUIRE(nl[ids[0]].name() == names[0]);
    REQUIRE(nl[ids[3]].name() == names[3]);
    REQUIRE_FALSE(nl[ids[1]].is_valid());
    REQUIRE_FALSE(nl[ids[2]].is_valid());

    // Fill the grown capacity. The next two allocations must reuse the tombstones.
    for (int i = 4; i < kGrownCap; ++i) {
        nl.new_vertex<NetlistVertexKind::NET>(std::string("filler") + std::to_string(i), NoData{});
    }

    auto reused0 = nl.new_vertex<NetlistVertexKind::NET>(std::string("reused0"), NoData{});
    auto reused1 = nl.new_vertex<NetlistVertexKind::NET>(std::string("reused1"), NoData{});
    REQUIRE(reused0 == ids[2]);
    REQUIRE(reused1 == ids[1]);
}

TEST_CASE("Edge growth relocates live data and preserves the free list", "[netlist]") {
    // See the vertex growth test for why capacities are multiples of the floor.
    constexpr int kBaseCap = 32;
    constexpr int kGrownCap = 64;

    Netlist nl;
    nl.ensure_edge_capacity(kBaseCap);

    auto a = nl.new_vertex<NetlistVertexKind::NET>(std::string("a"), NoData{});
    auto b = nl.new_vertex<NetlistVertexKind::NET>(std::string("b"), NoData{});
    auto c = nl.new_vertex<NetlistVertexKind::NET>(std::string("c"), NoData{});
    auto r = nl.new_vertex<NetlistVertexKind::R>(std::string("R1"), 1.0);

    auto live = nl.connect<NetlistEdgeKind::PIN_R>(r, a, NoData{}, 1.25);
    auto tombstone = nl.connect<NetlistEdgeKind::PIN_R>(r, b, NoData{}, 2.5);
    nl.remove(b);
    REQUIRE_FALSE(nl[tombstone].is_valid());

    // Force growth with a tombstoned edge present.
    nl.ensure_edge_capacity(kGrownCap);

    REQUIRE(nl[live].kind() == NetlistEdgeKind::PIN_R);
    REQUIRE(nl[live].net().name() == "a");
    REQUIRE(nl[live].trace_length() == 1.25);

    // Fill the grown capacity. The next edge must reuse the tombstone.
    for (int i = 2; i < kGrownCap; ++i) {
        nl.connect<NetlistEdgeKind::PIN_R>(r, c, NoData{}, 3.5);
    }
    auto r2 = nl.new_vertex<NetlistVertexKind::R>(std::string("R2"), 2.0);
    auto reused = nl.connect<NetlistEdgeKind::PIN_R>(r2, a, NoData{}, 5.5);
    REQUIRE(reused == tombstone);
    REQUIRE(nl[reused].trace_length() == 5.5);
}

TEST_CASE("copy_and_stitch copies, maps, and transfers adjacency", "[netlist]") {
    Netlist dst;
    auto this_join = dst.new_vertex<NetlistVertexKind::NET>(std::string("t_join"), NoData{});
    auto r0 = dst.new_vertex<NetlistVertexKind::R>(std::string("R0"), 5.0);
    auto preexisting = dst.connect<NetlistEdgeKind::PIN_R>(r0, this_join, NoData{}, 0.5);

    Netlist src;
    auto other_join = src.new_vertex<NetlistVertexKind::NET>(std::string("o_join"), NoData{});
    auto o_n1 = src.new_vertex<NetlistVertexKind::NET>(std::string("o_n1"), NoData{});
    auto o_r = src.new_vertex<NetlistVertexKind::R>(std::string("o_R"), 10.0);
    src.connect<NetlistEdgeKind::PIN_R>(o_r, other_join, NoData{}, 1.0);
    src.connect<NetlistEdgeKind::PIN_R>(o_r, o_n1, NoData{}, 2.0);

    struct Mapping : Netlist::CopyMappings {
        void map_v_name(std::string *name) const override { *name = "copied_" + *name; }
        void map_v_value(NetlistVertexKind, double *val) const override { *val *= 100.0; }
    } mappings;

    auto [first, second] = dst.copy_and_stitch(dst[this_join], src, src[other_join], &mappings);

    // o_join is consumed by the stitch, so only o_n1 and o_R are copied.
    REQUIRE(second.value - first.value == 2);

    auto o_n1_copy = dst.get_vertex("copied_o_n1");
    auto o_r_copy = dst.get_vertex("copied_o_R");
    REQUIRE(o_n1_copy.has_value());
    REQUIRE(o_r_copy.has_value());
    REQUIRE_FALSE(dst.get_vertex("copied_o_join").has_value());
    REQUIRE(first.value <= o_n1_copy->value);
    REQUIRE(o_n1_copy->value < second.value);
    REQUIRE(first.value <= o_r_copy->value);
    REQUIRE(o_r_copy->value < second.value);

    // map_v_value ran on the copied resistor.
    REQUIRE(*dst[*o_r_copy].get_data_if<NetlistVertexKind::R>() == 1000.0);

    // Adjacency: t_join keeps its own edge and gains the stitched one; the
    // copied resistor keeps both of its edges; the copied net keeps one.
    REQUIRE(dst[this_join].connections().size() == 2);
    REQUIRE(dst[*o_r_copy].connections().size() == 2);
    REQUIRE(dst[*o_n1_copy].connections().size() == 1);

    // The stitched edge must be reachable from both ends.
    std::optional<NetlistEdgeIdx> stitched;
    for (NetlistEdgeIdx e : dst[this_join].connections()) {
        if (dst[e].component().idx() == *o_r_copy) {
            stitched = e;
        }
    }
    REQUIRE(stitched.has_value());
    REQUIRE(dst[*stitched].net().idx() == this_join);

    // Removing t_join must clean up the stitched edge, not just the original.
    dst.remove(this_join);
    REQUIRE_FALSE(dst[*stitched].is_valid());
    REQUIRE_FALSE(dst[preexisting].is_valid());
}
