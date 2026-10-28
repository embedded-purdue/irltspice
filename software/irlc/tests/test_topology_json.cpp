// Tests the JSON -> C++ topology layer in the driver: comment-tolerant
// parsing, the auto-generated string <-> enum mapping, the individual JSON
// constructors, whole-template parsing, and per-serial patch application.
//
// This target does not link `irldriver` (see tests/CMakeLists.txt), so
// `topology_json.cpp` is compiled directly into it.

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string_view>

#include "irl/core/debug.hpp"
#include "irl/driver/builtin_topos.hpp"
#include "irl/driver/topology_json.hpp"

using namespace irl;
using namespace irl::driver;

namespace {

NetlistVertex vertex_named(Netlist &netlist, std::string_view name) {
    auto idx = netlist.get_vertex(name);
    REQUIRE(idx.has_value());
    return netlist[*idx];
}

NetlistEdge edge_to(Netlist &netlist, NetlistVertex const &component, std::string_view netName) {
    for (NetlistEdgeIdx idx : component.connections()) {
        NetlistEdge edge = netlist[idx];
        if (edge.net().name() == netName) {
            return edge;
        }
    }
    FAIL("No edge to net " << netName);
    return {};
}

NetlistVertex port_named(BoardTemplate const &tmpl, std::string_view name) {
    for (NetlistVertexIdx idx : tmpl.ports) {
        NetlistVertex port = tmpl.netlist[idx];
        if (port.name() == name) {
            return port;
        }
    }
    FAIL("No port named " << name);
    return {};
}

} // namespace

TEST_CASE("parseJsonc tolerates comments", "[topology][json]") {
    auto doc = parseJsonc(R"({
        // line comment
        "a": 1, /* block comment */
        "b": "x"
    })");

    REQUIRE(doc.at("a") == 1);
    REQUIRE(doc.at("b") == "x");
}

TEST_CASE("string -> enum mapping is generated", "[topology][json]") {
    REQUIRE(VertKindFromName("TOPO_XBAR") == NetlistVertexKind::TOPO_XBAR);
    REQUIRE(VertKindFromName("NET_V_GND") == NetlistVertexKind::NET_V_GND);
    REQUIRE_FALSE(VertKindFromName("NOT_A_KIND").has_value());

    REQUIRE(EdgeKindFromName("PIN_TOPO_XBAR") == NetlistEdgeKind::PIN_TOPO_XBAR);
    REQUIRE(EdgeKindFromName("PIN_PORT_MD_CHILD") == NetlistEdgeKind::PIN_PORT_MD_CHILD);
    REQUIRE_FALSE(EdgeKindFromName("NOPE").has_value());

    REQUIRE(XbarSideFromName("ROW") == XbarPinId::Side::ROW);
    REQUIRE(XbarSideFromName("COL") == XbarPinId::Side::COL);
    REQUIRE_FALSE(XbarSideFromName("UP").has_value());

    REQUIRE(DaughterKindFromName("BASIC_V1") == DaughterboardKind::BASIC_V1);
    REQUIRE(DaughterKindFromName("DIODE_V1") == DaughterboardKind::DIODE_V1);
    REQUIRE_FALSE(DaughterKindFromName("NOPE").has_value());

    REQUIRE(BoardClassFromName("MOTHER") == BoardClass::MOTHER);
    REQUIRE(BoardClassFromName("DAUGHTER") == BoardClass::DAUGHTER);
    REQUIRE_FALSE(BoardClassFromName("CHILD").has_value());
}

TEST_CASE("parseBoardTemplate reads the builtin mother", "[topology][json]") {
    auto doc = parseJsonc(mother_rev1);
    BoardTemplate tmpl = parseBoardTemplate(doc);

    REQUIRE(tmpl.kind.boardClass == BoardClass::MOTHER);
    REQUIRE(tmpl.kind.hardwareRevision == 0);
    REQUIRE(tmpl.kind.daughterKind == DaughterboardKind::UNKNOWN);

    Netlist &nl = tmpl.netlist;

    // The one crossbar, U1, has 7 row + 8 column pins.
    NetlistVertex u1 = vertex_named(nl, "U1");
    REQUIRE(u1.kind() == NetlistVertexKind::TOPO_XBAR);
    REQUIRE(u1.connections().size() == 15);

    auto *xbar = u1.get_data_if<NetlistVertexKind::TOPO_XBAR>();
    REQUIRE(xbar != nullptr);
    REQUIRE(xbar->ic_id == 1);
    REQUIRE(xbar->daughter_id == NO_DAUGHTERBOARD);
    REQUIRE(xbar->mother_id == NO_MOTHERBOARD);

    // First pin is the cir_in row.
    NetlistEdge firstPin = edge_to(nl, u1, "cir_in");
    REQUIRE(firstPin.kind() == NetlistEdgeKind::PIN_TOPO_XBAR);
    REQUIRE(firstPin.component().idx() == u1.idx());
    REQUIRE(firstPin.trace_length() == 0.0);
    auto const *firstPinData = firstPin.get_data_if<NetlistEdgeKind::PIN_TOPO_XBAR>();
    REQUIRE(firstPinData != nullptr);
    REQUIRE(firstPinData->side == XbarPinId::Side::ROW);
    REQUIRE(firstPinData->index == 0);

    // Last pin is the d4_2 column.
    NetlistEdge lastPin = edge_to(nl, u1, "d4_2");
    auto const *lastPinData = lastPin.get_data_if<NetlistEdgeKind::PIN_TOPO_XBAR>();
    REQUIRE(lastPinData != nullptr);
    REQUIRE(lastPinData->side == XbarPinId::Side::COL);
    REQUIRE(lastPinData->index == 7);

    // GND was recognized as a fixed rail, not a generic net.
    REQUIRE(vertex_named(nl, "GND").kind() == NetlistVertexKind::NET_V_GND);

    // 2 MM ports, 4 MD ports, 2 generic ports.
    REQUIRE(tmpl.ports.size() == 8);

    NetlistVertex daughter1 = port_named(tmpl, "DAUGHTER1");
    REQUIRE(daughter1.kind() == NetlistVertexKind::PORT_MD);
    auto const *daughterData = daughter1.get_data_if<NetlistVertexKind::PORT_MD>();
    REQUIRE(daughterData != nullptr);
    REQUIRE(daughterData->daughter_id == 0);
    REQUIRE(daughter1.connections().size() == 2);

    NetlistEdge daughterPin0 = edge_to(nl, daughter1, "d1_1");
    REQUIRE(daughterPin0.kind() == NetlistEdgeKind::PIN_PORT_MD_PARENT);
    auto const *daughterPin0Data = daughterPin0.get_data_if<NetlistEdgeKind::PIN_PORT_MD_PARENT>();
    REQUIRE(daughterPin0Data != nullptr);
    REQUIRE(*daughterPin0Data == 0);

    NetlistEdge daughterPin1 = edge_to(nl, daughter1, "d1_2");
    auto const *daughterPin1Data = daughterPin1.get_data_if<NetlistEdgeKind::PIN_PORT_MD_PARENT>();
    REQUIRE(daughterPin1Data != nullptr);
    REQUIRE(*daughterPin1Data == 1);

    NetlistVertex grandmother = port_named(tmpl, "from_grandmother1");
    REQUIRE(grandmother.kind() == NetlistVertexKind::PORT_MM);
    REQUIRE(grandmother.connections().size() == 1);
    NetlistEdge grandmotherPin = edge_to(nl, grandmother, "gma_out");
    REQUIRE(grandmotherPin.kind() == NetlistEdgeKind::PIN_PORT_MM_CHILD);
    auto const *grandmotherPinData =
        grandmotherPin.get_data_if<NetlistEdgeKind::PIN_PORT_MM_CHILD>();
    REQUIRE(grandmotherPinData != nullptr);
    REQUIRE(*grandmotherPinData == 0);

    NetlistVertex input = port_named(tmpl, "INPUT_PORT");
    REQUIRE(input.kind() == NetlistVertexKind::PORT_GENERIC);
    NetlistEdge inputPin = edge_to(nl, input, "cir_in");
    REQUIRE(inputPin.kind() == NetlistEdgeKind::PIN_PORT_GENERIC_PARENT);
    REQUIRE(inputPin.get_data_if<NetlistEdgeKind::PIN_PORT_GENERIC_PARENT>() != nullptr);
}

TEST_CASE("applyBoardPatch patches by unique id", "[topology][json]") {
    auto patches = parseJsonc(R"({
        "mother": {
            "1234": [
                { "op": "remove", "path": "/topology/components/U1" }
            ]
        }
    })");

    // Matching id: the component is removed.
    auto doc = parseJsonc(mother_rev1);
    applyBoardPatch(doc, BoardClass::MOTHER, 1234, patches);
    BoardTemplate patched = parseBoardTemplate(doc);
    REQUIRE_FALSE(patched.netlist.get_vertex("U1").has_value());

    // Unrelated id: untouched.
    auto untouched = parseJsonc(mother_rev1);
    applyBoardPatch(untouched, BoardClass::MOTHER, 999, patches);
    BoardTemplate untouchedTmpl = parseBoardTemplate(untouched);
    REQUIRE(untouchedTmpl.netlist.get_vertex("U1").has_value());

    // Wrong board class: untouched (this id is registered under mother only).
    auto asDaughter = parseJsonc(mother_rev1);
    applyBoardPatch(asDaughter, BoardClass::DAUGHTER, 1234, patches);
    BoardTemplate asDaughterTmpl = parseBoardTemplate(asDaughter);
    REQUIRE(asDaughterTmpl.netlist.get_vertex("U1").has_value());
}
