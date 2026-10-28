#include "irl/driver/translate_protobuf.hpp"

#include <cassert>
#include <cstddef>
#include <iterator>
#include <variant>

#include "command.pb.h"
#include "irl/command.hpp"
#include "irl/detail/util.hpp"
#include "irl/netlist.hpp"
#include "util/macros.hpp"

namespace irl {

#ifdef TRANSLATE_PROTO_TRACK_FIELDS
#include <set>

using google::protobuf::Descriptor;

std::set<const FieldDescriptor *> handled_fields{};

void insert_field_desc(Descriptor const *desc, const char *name) {
    FieldDescriptor const *fdesc = desc->FindFieldByName(name);
    IRL_ASSERT(fdesc != nullptr, "Null descriptor");
    handled_fields.insert(fdesc);
}
#define IDESC(BUF, NAME) insert_field_desc((BUF).descriptor(), #NAME)
#else
#define IDESC(BUF, NAME) ((void)0)
#endif

#define SET_FIELD(VAR, NAME, VALUE)                                                                \
    do {                                                                                           \
        (VAR).set_##NAME(VALUE);                                                                   \
        IDESC(VAR, NAME);                                                                          \
    } while (0);
// Uses comma operator to insert and then call getter in same expr
#define GET_FIELD(VAR, NAME) (IDESC(VAR, NAME), (VAR).NAME())
#define GET_MUTABLE(VAR, NAME) (IDESC(VAR, NAME), (VAR).mutable_##NAME())
#define HAS_FIELD(VAR, NAME) (IDESC(VAR, NAME), (VAR).has_##NAME())

static void initProgrammingCmdDest(ProgrammingCommandPB *out, auto const &dest);
static void initProgramXbar(ProgramXbarPB *out, ProgramXbar const &pxb);
static void initProgramSwitches(ProgramSwitchPB *out, ProgramPhysicalSwitches const &pxb);

void initProgrammingCmd(MotherCommandPB *out, ProgrammingCommand const &pcmd) {
    ProgrammingCommandPB *subout = GET_MUTABLE(*out, prog);
    std::visit(overloads{
                   [&](ProgramXbar const &pxb) {
                       initProgrammingCmdDest(subout, pxb.xbarId);
                       initProgramXbar(GET_MUTABLE(*GET_MUTABLE(*subout, payload), xbar), pxb);
                   },
                   [&](ProgramLogicalSwitch const &) {
                       IRL_ASSERT(
                           false,
                           "Internal error. initProgrammingCmd called with ProgramLogicalSwitch.");
                   },
                   [&](ProgramPhysicalSwitches const &psw) {
                       initProgrammingCmdDest(subout, psw.spst_ic_id);
                       initProgramSwitches(GET_MUTABLE(*GET_MUTABLE(*subout, payload), swcmd), psw);
                   },
               },
               pcmd.cmd);
}

void initProgrammingCmdDest(ProgrammingCommandPB *out, auto const &dest) {
    SET_FIELD(*out, motherboard_id, dest.mother_id);
    SET_FIELD(*out, daughterboard_id, dest.daughter_id);
}

void initProgramXbar(ProgramXbarPB *out, ProgramXbar const &pxb) {
    SET_FIELD(*out, column, pxb.col);
    SET_FIELD(*out, row, pxb.row);
    SET_FIELD(*out, crossbar_id, pxb.xbarId.ic_id);
    SET_FIELD(*out, do_connect, pxb.connect);
}

void initProgramSwitches(ProgramSwitchPB *out, ProgramPhysicalSwitches const &psw) {
    SET_FIELD(*out, switch_id, psw.spst_ic_id.ic_id);
    SET_FIELD(*out, active_bitmask, psw.active_bitmask);
    SET_FIELD(*out, onoff_bitmask, psw.onoff_bitmask);
}

void initStopProgrammingCmd(MotherCommandPB *out) { (void)GET_MUTABLE(*out, stop); }

void initTopoRequestCmd(MotherCommand *out) { (void)GET_MUTABLE(*out, topo); }

//======================================
// FUNCTIONS FOR READING FROM PROTOBUFS
//======================================

static void unpackMotherboardTopology(MotherboardTopology *out, MotherboardTopologyPB const &mtopo);
static void unpackDaughterboardTopology(DaughterboardTopology *out,
                                        DaughterboardTopologyPB const &dtopo);

IrlTopology unpackTopology(TopologyResponsePB const &topo, size_t sequence_no) {
    auto const &mothers = GET_FIELD(topo, motherboards);
    auto nMothers = mothers.size();

    Netlist netlist{};
    auto outMtopos = new MotherboardTopology[nMothers];

    size_t i = 0;
    for (MotherboardTopologyPB const &mtopo : mothers) {
        unpackMotherboardTopology(outMtopos + i, mtopo);
        i++;
    }

    // TODO:
    // INITIALIZE NETLIST BASED ON outMtopos

    return IrlTopology(outMtopos, nMothers, std::move(netlist), sequence_no);
};

// Unpack just protobuf stuff, leaves topology empty
void unpackMotherboardTopology(MotherboardTopology *out, MotherboardTopologyPB const &mtopo) {

    out->uniqueId = GET_FIELD(mtopo, unique_id);
    out->hardwareRevision = GET_FIELD(mtopo, hardware_revision);
    out->firmwareVersion.major = GET_FIELD(mtopo, firm_version_major);
    out->firmwareVersion.minor = GET_FIELD(mtopo, firm_version_minor);
    out->firmwareVersion.patch = GET_FIELD(mtopo, firm_version_patch);

    auto const &daughters = GET_FIELD(mtopo, daughterboards);
    auto nMothers = daughters.size();
    IRL_ASSERT(nMothers == std::size(out->daughterboards), "Got bad number of daughterboards.");

    size_t i = 0;
    for (MotherboardTopology_NullableDaughterboardTopology const &ndtopo : daughters) {
        if (HAS_FIELD(ndtopo, empty)) {
            out->daughterboards[i] = {0};
            out->daughterboards[i].kind = DaughterboardKind::NOT_PRESENT;
        } else {
            IRL_ASSERT(HAS_FIELD(ndtopo, daughterboard_info), "Bad daughterboard info.");
            unpackDaughterboardTopology(out->daughterboards + i,
                                        GET_FIELD(ndtopo, daughterboard_info));
        }
        i++;
    }
}

static void unpackDaughterboardTopology(DaughterboardTopology *out,
                                        DaughterboardTopologyPB const &dtopo) {

    out->uniqueId = GET_FIELD(dtopo, unique_id);
    out->firmwareVersion.major = GET_FIELD(dtopo, firm_version_major);
    out->firmwareVersion.minor = GET_FIELD(dtopo, firm_version_minor);
    out->firmwareVersion.patch = GET_FIELD(dtopo, firm_version_patch);

    if (HAS_FIELD(dtopo, basic_daughter)) {
        out->kind = DaughterboardKind::BASIC_V1;
    } else if (HAS_FIELD(dtopo, diode_daughter)) {
        out->kind = DaughterboardKind::DIODE_V1;
    } else if (HAS_FIELD(dtopo, test_daughter)) {
        out->kind = DaughterboardKind::TESTING_V1;
    } else {
        IRL_ASSERT(0, "Invalid daughterboard kind detected.");
    }
}

} // namespace irl
