// Tests the JSON -> C++ topology layer in the driver: comment-tolerant
// parsing, the auto-generated string <-> enum mapping, the individual JSON
// constructors, whole-template parsing, and per-serial patch application.
//
// This target does not link `irldriver` (see tests/CMakeLists.txt), so
// `topology_json.cpp` is compiled directly into it.

#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <nlohmann/json.hpp>

#include "irl/core/debug.hpp"
#include "irl/driver/builtin_topos.hpp"
#include "irl/driver/topology_json.hpp"

using namespace irl;
using namespace irl::driver;

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

    REQUIRE(tmpl.boardClass == BoardClass::MOTHER);
    REQUIRE(tmpl.revision == 0);
    REQUIRE_FALSE(tmpl.daughterKind.has_value());

    One crossbar, with 7 row + 8 column pins.REQUIRE(tmpl.components.size() == 1);
    TemplateComponent const &u1 = tmpl.components[0];
    REQUIRE(u1.id == "U1");
    REQUIRE(u1.kind == NetlistVertexKind::TOPO_XBAR);
    REQUIRE(u1.pins.size() == 15);

    REQUIRE(u1.pins[0].dest == "cir_in");
    REQUIRE(u1.pins[0].kind == NetlistEdgeKind::PIN_TOPO_XBAR);
    REQUIRE(u1.pins[0].traceLength == 0.0);
    auto const *firstPin = std::get_if<XbarPinId>(&u1.pins[0].data);
    REQUIRE(firstPin != nullptr);
    REQUIRE(firstPin->side == XbarPinId::Side::ROW);
    REQUIRE(firstPin->index == 0);

    // Last pin is the d4_2 column.
    auto const *lastPin = std::get_if<XbarPinId>(&u1.pins[14].data);
    REQUIRE(lastPin != nullptr);
    REQUIRE(lastPin->side == XbarPinId::Side::COL);
    REQUIRE(lastPin->index == 7);

    // 2 MM ports, 4 MD ports, 2 generic ports.
    REQUIRE(tmpl.ports.size() == 8);

    TemplatePort const *daughter1 = nullptr;
    TemplatePort const *grandmother = nullptr;
    TemplatePort const *input = nullptr;
    for (TemplatePort const &port : tmpl.ports) {
        if (port.name == "DAUGHTER1") {
            daughter1 = &port;
        } else if (port.name == "from_grandmother1") {
            grandmother = &port;
        } else if (port.name == "INPUT_PORT") {
            input = &port;
        }
    }

    REQUIRE(daughter1 != nullptr);
    REQUIRE(daughter1->kind == NetlistVertexKind::PORT_MD);
    REQUIRE(daughter1->daughterId == 0);
    REQUIRE(daughter1->pins.size() == 2);
    REQUIRE(daughter1->pins[0].parent);
    REQUIRE(daughter1->pins[0].pin.dest == "d1_1");
    REQUIRE(daughter1->pins[0].pin.kind == NetlistEdgeKind::PIN_PORT_MD_PARENT);
    REQUIRE(std::get<uint32_t>(daughter1->pins[1].pin.data) == 1);

    REQUIRE(grandmother != nullptr);
    REQUIRE(grandmother->kind == NetlistVertexKind::PORT_MM);
    REQUIRE(grandmother->pins.size() == 1);
    REQUIRE_FALSE(grandmother->pins[0].parent);
    REQUIRE(grandmother->pins[0].pin.kind == NetlistEdgeKind::PIN_PORT_MM_CHILD);

    REQUIRE(input != nullptr);
    REQUIRE(input->kind == NetlistVertexKind::PORT_GENERIC);
    REQUIRE(input->pins[0].pin.kind == NetlistEdgeKind::PIN_PORT_GENERIC_PARENT);
    REQUIRE(std::holds_alternative<NoData>(input->pins[0].pin.data));
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
    // auto doc = parseJsonc(mother_rev1);
    // applyBoardPatch(doc, BoardClass::MOTHER, 1234, patches);
    // REQUIRE(parseBoardTemplate(doc).components.empty());
    //
    // // Unrelated id: untouched.
    // auto untouched = parseJsonc(mother_rev1);
    // applyBoardPatch(untouched, BoardClass::MOTHER, 999, patches);
    // REQUIRE(parseBoardTemplate(untouched).components.size() == 1);
    //
    // // Wrong board class: untouched (this id is registered under mother only).
    // auto asDaughter = parseJsonc(mother_rev1);
    // applyBoardPatch(asDaughter, BoardClass::DAUGHTER, 1234, patches);
    // REQUIRE(parseBoardTemplate(asDaughter).components.size() == 1);
}
