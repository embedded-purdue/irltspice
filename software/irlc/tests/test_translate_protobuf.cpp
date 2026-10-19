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
#include <set>

// TRANSLATE_PROTO_TRACK_FIELDS is set by the build for this target only; see
// tests/CMakeLists.txt.
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
        CHECK(handled_fields.count(field) == 1);

        if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE) {
            checkAllFieldsHandled(field->message_type(), visited);
        }
    }
}

void checkAllFieldsHandled(const Descriptor *desc) {
    std::set<const Descriptor *> visited;
    checkAllFieldsHandled(desc, visited);
}

} // namespace

TEST_CASE("Pack every MotherCommand variant", "[translate][proto]") {
    handled_fields.clear();

    // MotherCommand.cmd: topo
    ::MotherCommand topoCmd{};
    initTopoRequestCmd(&topoCmd);

    // MotherCommand.cmd: stop
    ::MotherCommand stopCmd{};
    initStopProgrammingCmd(&stopCmd);

    // ProgrammingPayload.data: xbar
    irl::XbarId xbarId{};
    xbarId.mother_id = 1;
    xbarId.daughter_id = 2;
    xbarId.ic_id = 3;

    irl::ProgramXbar xbar{};
    xbar.xbarId = xbarId;
    xbar.row = 4;
    xbar.col = 5;
    xbar.connect = true;

    irl::ProgrammingCommand xbarCmd{.cmd = xbar};
    ::MotherCommand xbarOut{};
    initProgrammingCmd(&xbarOut, xbarCmd);

    // ProgrammingPayload.data: swcmd
    irl::SpstId spstId{};
    spstId.mother_id = 1;
    spstId.daughter_id = 2;
    spstId.ic_id = 3;
    spstId.sw_id = 0;

    irl::ProgramPhysicalSwitches switches{};
    switches.spst_ic_id = spstId;
    switches.active_bitmask = 0xF0;
    switches.onoff_bitmask = 0x0F;

    irl::ProgrammingCommand switchCmd{.cmd = switches};
    ::MotherCommand switchOut{};
    initProgrammingCmd(&switchOut, switchCmd);

    checkAllFieldsHandled(::MotherCommand::descriptor());
}

TEST_CASE("Unpack a full topology", "[translate][proto]") {
    handled_fields.clear();

    ::TopologyResponse resp{};

    // Motherboard has exactly four daughterboard slots.
    ::MotherboardTopology *mtopo = resp.add_motherboards();
    mtopo->set_unique_id(0xAABBCCDD);
    mtopo->set_hardware_revision(7);
    mtopo->set_firm_version_major(1);
    mtopo->set_firm_version_minor(2);
    mtopo->set_firm_version_patch(3);

    // NullableDaughterboardTopology.daughterboard: empty
    mtopo->add_daughterboards()->mutable_empty();

    // NullableDaughterboardTopology.daughterboard: daughterboard_info
    // DaughterboardTopology.info: basic_daughter
    ::DaughterboardTopology *basic = mtopo->add_daughterboards()->mutable_daughterboard_info();
    basic->set_unique_id(101);
    basic->set_firm_version_major(1);
    basic->set_firm_version_minor(0);
    basic->set_firm_version_patch(0);
    basic->mutable_basic_daughter();

    // DaughterboardTopology.info: diode_daughter
    ::DaughterboardTopology *diode = mtopo->add_daughterboards()->mutable_daughterboard_info();
    diode->set_unique_id(202);
    diode->set_firm_version_major(1);
    diode->set_firm_version_minor(1);
    diode->set_firm_version_patch(0);
    diode->mutable_diode_daughter();

    // DaughterboardTopology.info: test_daughter
    ::DaughterboardTopology *test = mtopo->add_daughterboards()->mutable_daughterboard_info();
    test->set_unique_id(303);
    test->set_firm_version_major(1);
    test->set_firm_version_minor(2);
    test->set_firm_version_patch(0);
    test->mutable_test_daughter();

    irl::IrlTopology topo = unpackTopology(resp, 42);

    checkAllFieldsHandled(::TopologyResponse::descriptor());
}
