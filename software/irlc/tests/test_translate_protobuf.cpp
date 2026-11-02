// Tests that the protobuf <-> public-API translators touch every field of
// every protobuf message reachable from the message they translate.
//
// Every translate call records the fields it handles in `handled_fields` (see
// translate_protobuf.cpp). Each test clears that set, runs the translator over
// every branch it wants to cover (oneofs need one call per variant, and the
// set is *not* cleared between calls), then runs the shared verification
// helper once. If a translator silently drops a field, the helper fails.

#include <google/protobuf/descriptor.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <string_view>

// TRANSLATE_PROTO_TRACK_FIELDS is set by the build for this target only; see
// tests/CMakeLists.txt.
#include "irl/command.hpp"
#include "irl/driver/topology_json.hpp"
#include "irl/driver/translate_protobuf.hpp"

namespace {

using ::google::protobuf::Descriptor;
using ::google::protobuf::FieldDescriptor;

// Walks every message reachable from `desc` through its message-typed fields
// and asserts each field was recorded as handled. Run this *after* all calls
// needed to cover the message's oneofs.
void checkAllFieldsHandled(const Descriptor *desc, std::set<const Descriptor *> &visited) {
    if (!visited.insert(desc).second) {
        return;
    }

    for (int i = 0; i < desc->field_count(); ++i) {
        const FieldDescriptor *field = desc->field(i);
        INFO("Unhandled protobuf field: " << field->full_name());
        CHECK(irl::driver::handled_fields.count(field) == 1);

        if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE) {
            checkAllFieldsHandled(field->message_type(), visited);
        }
    }
}

void checkAllFieldsHandled(const Descriptor *desc) {
    std::set<const Descriptor *> visited;
    checkAllFieldsHandled(desc, visited);
}

// A deliberately implausible motherboard revision. The repo's builtin
// topologies are still a WIP, so tests register their own templates instead of
// relying on them.
constexpr uint32_t kAbsurdMotherRevision = 999999;

// A minimal motherboard template that exercises every port flavor the
// unpacker stitches: a child-side MM port (to the grandmother), a parent-side
// MM port (to the next motherboard), and an MD port for each of the four
// daughterboard slots (so empty slots must be skipped).
nlohmann::json absurd_mother_json() {
    nlohmann::json doc = nlohmann::json::parse(R"({
        "class": "MOTHER",
        "topology": {
            "components": {
                "U1": {
                    "kind": "TOPO_XBAR",
                    "ic_id": 1,
                    "pins": [
                        { "dest": "in",  "side": "ROW", "index": 0, "len": 0.0 },
                        { "dest": "out", "side": "ROW", "index": 1, "len": 0.0 },
                        { "dest": "d0",  "side": "COL", "index": 0, "len": 0.0 },
                        { "dest": "d1",  "side": "COL", "index": 1, "len": 0.0 },
                        { "dest": "d2",  "side": "COL", "index": 2, "len": 0.0 },
                        { "dest": "d3",  "side": "COL", "index": 3, "len": 0.0 }
                    ]
                }
            },
            "ports": {
                "from_grandmother": { "kind": "MM", "pins": [
                    { "kind": "CHILD", "dest": "in", "index": 0, "len": 0.0 }
                ] },
                "to_mother": { "kind": "MM", "pins": [
                    { "kind": "PARENT", "dest": "out", "index": 0, "len": 0.0 }
                ] },
                "DAUGHTER0": { "kind": "MD", "daughter_id": 0, "pins": [
                    { "kind": "PARENT", "dest": "d0", "index": 0, "len": 0.0 }
                ] },
                "DAUGHTER1": { "kind": "MD", "daughter_id": 1, "pins": [
                    { "kind": "PARENT", "dest": "d1", "index": 0, "len": 0.0 }
                ] },
                "DAUGHTER2": { "kind": "MD", "daughter_id": 2, "pins": [
                    { "kind": "PARENT", "dest": "d2", "index": 0, "len": 0.0 }
                ] },
                "DAUGHTER3": { "kind": "MD", "daughter_id": 3, "pins": [
                    { "kind": "PARENT", "dest": "d3", "index": 0, "len": 0.0 }
                ] }
            }
        }
    })");
    doc["revision"] = kAbsurdMotherRevision;
    return doc;
}

// A minimal daughterboard template with a single child-side MD port, for the
// requested daughterboard kind.
nlohmann::json absurd_daughter_json(std::string_view kind) {
    nlohmann::json doc = nlohmann::json::parse(R"({
        "class": "DAUGHTER",
        "topology": {
            "components": {
                "U9": {
                    "kind": "TOPO_XBAR",
                    "ic_id": 9,
                    "pins": [
                        { "dest": "d_in", "side": "ROW", "index": 0, "len": 0.0 },
                        { "dest": "from_mother", "side": "COL", "index": 0, "len": 0.0 }
                    ]
                }
            },
            "ports": {
                "J1": { "kind": "MD", "daughter_id": 0, "pins": [
                    { "kind": "CHILD", "dest": "from_mother", "index": 0, "len": 0.0 }
                ] }
            }
        }
    })");
    doc["kind"] = std::string(kind);
    return doc;
}

enum class DaughterSlot { Empty, Basic, Diode, Testing };

// Fill one daughterboard slot of a proto topology with the requested variant.
void set_slot(::MotherboardTopology_NullableDaughterboardTopology &slot, DaughterSlot kind,
              uint64_t uniqueId) {
    if (kind == DaughterSlot::Empty) {
        slot.mutable_empty();
        return;
    }

    ::DaughterboardTopology *daughter = slot.mutable_daughterboard_info();
    daughter->set_unique_id(uniqueId);
    daughter->set_firm_version_major(1);
    daughter->set_firm_version_minor(2);
    daughter->set_firm_version_patch(3);

    switch (kind) {
    case DaughterSlot::Basic:
        daughter->mutable_basic_daughter();
        break;
    case DaughterSlot::Diode:
        daughter->mutable_diode_daughter();
        break;
    case DaughterSlot::Testing:
        daughter->mutable_test_daughter();
        break;
    case DaughterSlot::Empty:
        break;
    }
}

// Add a motherboard with the absurd revision and four daughter slots, the
// first configured via `slotKind`/`slotUniqueId` and the rest empty.
::MotherboardTopology *add_motherboard(::TopologyResponse &resp, uint64_t uniqueId,
                                       DaughterSlot slotKind, uint64_t slotUniqueId) {
    ::MotherboardTopology *mother = resp.add_motherboards();
    mother->set_unique_id(uniqueId);
    mother->set_hardware_revision(kAbsurdMotherRevision);
    mother->set_firm_version_major(1);
    mother->set_firm_version_minor(0);
    mother->set_firm_version_patch(0);

    set_slot(*mother->add_daughterboards(), slotKind, slotUniqueId);
    for (int i = 0; i < 3; ++i) {
        mother->add_daughterboards()->mutable_empty();
    }
    return mother;
}

// The edge connecting `component` to the net named `netName`, or a test
// failure if there is none.
irl::NetlistEdge require_edge_to(irl::Netlist &netlist, irl::NetlistVertex const &component,
                                 std::string_view netName) {
    for (irl::NetlistEdgeIdx idx : component.connections()) {
        irl::NetlistEdge edge = netlist[idx];
        if (edge.net().name() == netName) {
            return edge;
        }
    }
    FAIL("No edge from " << component.name() << " to net " << netName);
    return {};
}

// A database preloaded with the absurd mother and every daughterboard kind the
// topology translator knows about.
irl::driver::LazyTopologyDB make_absurd_db() {
    irl::driver::LazyTopologyDB db;
    db.register_new_kind(irl::driver::BoardKindId::motherboard(kAbsurdMotherRevision),
                         absurd_mother_json());
    db.register_new_kind(irl::driver::BoardKindId::daughterboard(irl::DaughterboardKind::BASIC_V1),
                         absurd_daughter_json("BASIC_V1"));
    db.register_new_kind(irl::driver::BoardKindId::daughterboard(irl::DaughterboardKind::DIODE_V1),
                         absurd_daughter_json("DIODE_V1"));
    db.register_new_kind(
        irl::driver::BoardKindId::daughterboard(irl::DaughterboardKind::TESTING_V1),
        absurd_daughter_json("TESTING_V1"));
    return db;
}

} // namespace

TEST_CASE("Pack every MotherCommand variant", "[translate][proto]") {
    irl::driver::handled_fields.clear();

    // MotherCommand.cmd: topo
    ::MotherCommand topoCmd{};
    irl::driver::initTopoRequestCmd(&topoCmd);

    // MotherCommand.cmd: stop
    ::MotherCommand stopCmd{};
    irl::driver::initStopProgrammingCmd(&stopCmd);

    // ProgrammingPayload.data: xbar
    irl::XbarId xbarId{};
    xbarId.board_id.mother_id = 1;
    xbarId.board_id.daughter_id = 2;
    xbarId.ic_id = 3;

    irl::ProgramXbar xbar{};
    xbar.xbarId = xbarId;
    xbar.row = 4;
    xbar.col = 5;
    xbar.connect = true;

    irl::ProgrammingCommand xbarCmd{.cmd = xbar};
    ::MotherCommand xbarOut{};
    irl::driver::initProgrammingCmd(&xbarOut, xbarCmd);

    // ProgrammingPayload.data: swcmd
    irl::SpstId spstId{};
    spstId.board_id.mother_id = 1;
    spstId.board_id.daughter_id = 2;
    spstId.ic_id = 3;
    spstId.sw_id = 0;

    irl::ProgramPhysicalSwitches switches{};
    switches.spst_ic_id = spstId;
    switches.active_bitmask = 0xF0;
    switches.onoff_bitmask = 0x0F;

    irl::ProgrammingCommand switchCmd{.cmd = switches};
    ::MotherCommand switchOut{};
    irl::driver::initProgrammingCmd(&switchOut, switchCmd);

    // ProgrammingPayload.data: rcmd
    irl::ResistorId resistorId{};
    resistorId.board_id.mother_id = 1;
    resistorId.board_id.daughter_id = 2;
    resistorId.ic_id = 3;
    resistorId.r_id = 4;

    irl::ProgramResistor resistor{};
    resistor.resistor_id = resistorId;
    resistor.resistance = 1000.0;

    irl::ProgrammingCommand resistorCmd{.cmd = resistor};
    ::MotherCommand resistorOut{};
    irl::driver::initProgrammingCmd(&resistorOut, resistorCmd);

    REQUIRE(resistorOut.prog().motherboard_id() == 1u);
    REQUIRE(resistorOut.prog().daughterboard_id() == 2u);
    REQUIRE(resistorOut.prog().payload().rcmd().ic_id() == 3u);
    REQUIRE(resistorOut.prog().payload().rcmd().resistor_id() == 4u);
    REQUIRE(resistorOut.prog().payload().rcmd().resistance() == 1000u);

    checkAllFieldsHandled(::MotherCommand::descriptor());
}

TEST_CASE("Unpack a full topology", "[translate][proto]") {
    irl::driver::handled_fields.clear();

    irl::driver::LazyTopologyDB db = make_absurd_db();

    // Run the translator once per daughterboard oneof variant. The tracked set
    // is not cleared between calls, so this covers every branch.
    auto unpack_with_slot = [&](DaughterSlot slotKind, uint64_t slotUniqueId) {
        ::TopologyResponse resp{};
        add_motherboard(resp, 0xAABBCCDD, slotKind, slotUniqueId);
        (void)irl::driver::TopoUnpacker::unpackTopology(resp, db, 0);
    };

    // DaughterboardTopology.info: basic_daughter
    unpack_with_slot(DaughterSlot::Basic, 101);

    // DaughterboardTopology.info: diode_daughter
    unpack_with_slot(DaughterSlot::Diode, 202);

    // DaughterboardTopology.info: test_daughter
    unpack_with_slot(DaughterSlot::Testing, 303);

    // NullableDaughterboardTopology.daughterboard: empty is covered by the
    // three empty trailing slots of every response above.

    checkAllFieldsHandled(::TopologyResponse::descriptor());
}

TEST_CASE("unpackTopology stitches mothers and daughters into one netlist",
          "[translate][proto][topology]") {
    irl::driver::LazyTopologyDB db;
    db.register_new_kind(irl::driver::BoardKindId::motherboard(kAbsurdMotherRevision),
                         absurd_mother_json());
    db.register_new_kind(
        irl::driver::BoardKindId::daughterboard(irl::DaughterboardKind::TESTING_V1),
        absurd_daughter_json("TESTING_V1"));

    // Two stacked motherboards, each carrying a TESTING_V1 daughter in slot 0.
    ::TopologyResponse resp{};
    constexpr uint64_t firstMotherId = 0x1234;
    constexpr uint64_t secondMotherId = 0x5678;
    add_motherboard(resp, firstMotherId, DaughterSlot::Testing, 0x1111);
    add_motherboard(resp, secondMotherId, DaughterSlot::Testing, 0x2222);

    irl::IrlTopology topo = irl::driver::TopoUnpacker::unpackTopology(resp, db, 42);

    REQUIRE(topo[0].uniqueId == firstMotherId);
    REQUIRE(topo[0].hardwareRevision == kAbsurdMotherRevision);
    REQUIRE(topo[1].uniqueId == secondMotherId);
    REQUIRE(topo[1].hardwareRevision == kAbsurdMotherRevision);
    REQUIRE(topo[1].daughterboards[0].kind == irl::DaughterboardKind::TESTING_V1);
    REQUIRE(topo[1].daughterboards[0].uniqueId == 0x2222);

    irl::Netlist &nl = topo.netlist();

    // Every board's components were copied under a per-board name prefix.
    REQUIRE(nl.get_vertex("m0:U1").has_value());
    REQUIRE(nl.get_vertex("m1:U1").has_value());
    REQUIRE(nl.get_vertex("m0d0:U9").has_value());
    REQUIRE(nl.get_vertex("m1d0:U9").has_value());

    irl::NetlistVertex root = nl[nl.get_vertex("m0:port_root").value()];
    REQUIRE(root.kind() == irl::NetlistVertexKind::PORT_MM);
    irl::NetlistEdge rootEdge = require_edge_to(nl, root, "m0:in");
    REQUIRE(rootEdge.kind() == irl::NetlistEdgeKind::PIN_PORT_MM_CHILD);

    // Motherboard 0's parent-side MM port was stitched to motherboard 1's
    // child side. This is the case that exercises the multi-motherboard walk.
    irl::NetlistVertex m0ToMother = nl[nl.get_vertex("m0:to_mother").value()];
    irl::NetlistEdge upEdge = require_edge_to(nl, m0ToMother, "m1:in");
    REQUIRE(upEdge.kind() == irl::NetlistEdgeKind::PIN_PORT_MM_CHILD);

    // The daughter template was stitched onto motherboard 0's MD port.
    irl::NetlistVertex daughterPort = nl[nl.get_vertex("m0:DAUGHTER0").value()];
    REQUIRE(daughterPort.kind() == irl::NetlistVertexKind::PORT_MD);
    irl::NetlistEdge daughterEdge = require_edge_to(nl, daughterPort, "m0d0:from_mother");
    REQUIRE(daughterEdge.kind() == irl::NetlistEdgeKind::PIN_PORT_MD_CHILD);

    // The daughter's own component is wired to its own net.
    irl::NetlistVertex daughterComponent = nl[nl.get_vertex("m0d0:U9").value()];
    irl::NetlistEdge daughterCompEdge = require_edge_to(nl, daughterComponent, "m0d0:d_in");
    REQUIRE(daughterCompEdge.component().idx() == daughterComponent.idx());

    // Empty daughter slots still have MD ports on the template, but nothing is
    // stitched into them.
    irl::NetlistVertex emptyDaughterPort = nl[nl.get_vertex("m0:DAUGHTER1").value()];
    REQUIRE(emptyDaughterPort.kind() == irl::NetlistVertexKind::PORT_MD);
    REQUIRE(emptyDaughterPort.connections().size() == 1);
    REQUIRE_FALSE(nl.get_vertex("m0d1:U9").has_value());
    REQUIRE_FALSE(nl.get_vertex("m1d2:U9").has_value());
}
