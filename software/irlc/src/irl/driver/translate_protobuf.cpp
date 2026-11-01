#include "irl/driver/translate_protobuf.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "command.pb.h"
#include "irl/command.hpp"
#include "irl/detail/util.hpp"
#include "irl/driver/topology_db.hpp"
#include "irl/netlist.hpp"
#include "util/boost_util.hpp"
#include "util/macros.hpp"

namespace irl::driver {

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
    SET_FIELD(*out, motherboard_id, dest.board_id.mother_id);
    SET_FIELD(*out, daughterboard_id, dest.board_id.daughter_id);
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

// Mappings to use for a motherboard
class MotherMappings : public Netlist::CopyMappings {
  public:
    MotherId_t mCurrentMother;

    MotherMappings(MotherId_t m) : mCurrentMother(m) {};

    virtual void map_v_name(std::string *name) const override {
        name->insert(0, std::format("m{}:", mCurrentMother));
    }

    virtual void map_v_value(NetlistVertexKind kind, XbarId *val) const override {
        val->board_id.mother_id = mCurrentMother;
        val->board_id.daughter_id = NO_DAUGHTERBOARD;
    }
    virtual void map_v_value(NetlistVertexKind kind, SpstId *val) const override {
        val->board_id.mother_id = mCurrentMother;
        val->board_id.daughter_id = NO_DAUGHTERBOARD;
    }
    virtual void map_v_value(NetlistVertexKind kind, BoardId *val) const override {
        val->mother_id = mCurrentMother;
        // Don't alter the daughter as that should be in the template
    }
    virtual void map_v_value(NetlistVertexKind kind, MMPortId *val) const override {
        // Nop. We should handle these outside as they are the ones we stitch on
        return;
    }
};

// Mappings to use for a daughterboard
class DaughterMappings : public Netlist::CopyMappings {
  public:
    MotherId_t mCurrentMother;
    DaughterId_t mCurrentDaughter;

    DaughterMappings(MotherId_t m, DaughterId_t d) : mCurrentMother(m), mCurrentDaughter(d) {};

    virtual void map_v_name(std::string *name) const override {
        name->insert(0, std::format("m{}d{}:", mCurrentMother, mCurrentDaughter));
    }

    virtual void map_v_value(NetlistVertexKind kind, XbarId *val) const override {
        val->board_id.mother_id = mCurrentMother;
        val->board_id.daughter_id = mCurrentDaughter;
    }
    virtual void map_v_value(NetlistVertexKind kind, SpstId *val) const override {
        val->board_id.mother_id = mCurrentMother;
        val->board_id.daughter_id = mCurrentDaughter;
    }
    virtual void map_v_value(NetlistVertexKind kind, BoardId *val) const override {
        IRL_ASSERT(false, "Daughterboard contains multiple MD ports");
    }
    virtual void map_v_value(NetlistVertexKind kind, MMPortId *val) const override {
        // Nop. We should handle these outside as they are the ones we stitch on
        return;
    }
};

static NetlistVertexIdx
locate_port(Netlist const &netlist, irl::NetlistVertexKind kind,
            irl::NetlistEdgeKind child_or_parent,
            std::pair<Netlist::VertexIter, Netlist::VertexIter> board_span) {
    for (auto i = board_span.first; i != board_span.second; i++) {
        NetlistVertex v = *i;
        if (v.kind() == NetlistVertexKind::PORT_MM) {

            bool has_right_kind =
                std::ranges::find_if(v.connections(), [&](NetlistEdgeIdx const &e_i) {
                    return (*v.netlist())[e_i].kind() == child_or_parent;
                }) == v.connections().end();

            if (has_right_kind) {
                return v.idx();
            }
        }
    }
    return {}; // null
}

IrlTopology TopoUnpacker::unpackTopology(TopologyResponsePB const &topo, LazyTopologyDB const &db,
                                         size_t sequence_no) {
    auto const &mothers = GET_FIELD(topo, motherboards);
    auto nMothers = mothers.size();

    IrlTopology out(new MotherboardTopology[nMothers], nMothers, {}, sequence_no);

    size_t i = 0;
    for (MotherboardTopologyPB const &mtopo : mothers) {
        unpackMotherboardTopology(out.mMotherboards + i, mtopo);
        i++;
    }

    // initialize netlist based on outMtopos

    // We need to rebuild the netlist by stitching together templates from the DB
    // Start with a MM node that goes to no grandmother (the root)
    NetlistVertexIdx root_node = out.mNetlist.new_vertex<NetlistVertexKind::PORT_MM>(
        "m0:port_root", MMPortId{
                            .grandmother_id = NO_MOTHERBOARD,
                            .childmother_id = 0,
                        });
    // Make a mock span
    const BoardSpan mock_root_span{
        .boardId = {},
        .span = {0, 1},
    };
    std::pair<Netlist::VertexIter, Netlist::VertexIter> prev_mother_span = {
        {out.mNetlist, 0},
        {out.mNetlist, 1},
    };

    for (MotherId_t m_i = 0; m_i < nMothers; m_i++) {

        // Step 1:
        // Locate the MM port on the previous motherboard
        NetlistVertexIdx parent_mm =
            locate_port(out.mNetlist, NetlistVertexKind::PORT_MM,
                        NetlistEdgeKind::PIN_PORT_MM_PARENT, prev_mother_span);
        IRL_ASSERT(!parent_mm.is_null(), "Motherboard topology {} has no parent MM ports.",
                   m_i - 1);

        // Step 2:
        // Get the template
        auto const &mtopo = out.mMotherboards[m_i];
        std::optional template_o = db.get_template_for_board(BoardGUID::from(mtopo));
        IRL_ASSERT(template_o.has_value(),
                   "Received topology for revision: {} & uniqueid: {} not in db",
                   mtopo.hardwareRevision, mtopo.uniqueId);
        BoardTemplate const &boardTemplate = *template_o;

        // Step 3:
        // Locate the MM port on the template
        Netlist const &motherTemplateNl = boardTemplate.netlist;
        NetlistVertexIdx child_mm =
            locate_port(out.mNetlist, NetlistVertexKind::PORT_MM,
                        NetlistEdgeKind::PIN_PORT_MM_CHILD, boardTemplate.netlist.vertices());
        IRL_ASSERT(!child_mm.is_null(), "Motherboard topology {} has no child MM ports.", m_i);

        // Step 4:
        // Stitch the current motherboard onto the previous motherboard's MM port
        MotherMappings mms(m_i);
        auto new_nodes_i = out.mNetlist.copy_and_stitch(out.mNetlist[parent_mm], motherTemplateNl,
                                                        motherTemplateNl[child_mm], &mms);
        prev_mother_span = {
            {out.mNetlist, new_nodes_i.first.value},
            {out.mNetlist, new_nodes_i.second.value},
        };
        out.mBoardSpans.push_back({
            .boardId = {.mother_id = m_i, .daughter_id = NO_DAUGHTERBOARD},
            .span = new_nodes_i,
        });

        // Step 5:
        // Find all MD ports on current. For each MD port:
        for (auto port : pair_to_iter(prev_mother_span)) {
            if (port.kind() == NetlistVertexKind::PORT_MD) {
                // Found an md port on mother
                // 5.a: Assert that the mother is the parent of this MD port
                IRL_ASSERT(out.mNetlist[port.connections()[0]].kind() ==
                               NetlistEdgeKind::PIN_PORT_MD_PARENT,
                           "Non-parent MD port found on mother: {}", m_i);

                auto const d_i = port.get_data_if<NetlistVertexKind::PORT_MD>()->daughter_id;

                // 5.b: find a template for that daughter and locate the child MD port on the
                // template
                auto const &dtopo =
                    mtopo.daughterboards[port.get_data_if<NetlistVertexKind::PORT_MD>()
                                             ->daughter_id];
                std::optional d_template_o = db.get_template_for_board(BoardGUID::from(dtopo));
                IRL_ASSERT(d_template_o.has_value(),
                           "Received topology for daughter: {} & uniqueid: {} not in db",
                           DAUGHTER_KIND_NAMES[(uint32_t)dtopo.kind].first, dtopo.uniqueId);
                BoardTemplate const &daughterboardTemplate = *d_template_o;

                Netlist const &daughterTemplateNl = daughterboardTemplate.netlist;

                NetlistVertexIdx daughter_md =
                    locate_port(daughterTemplateNl, NetlistVertexKind::PORT_MD,
                                NetlistEdgeKind::PIN_PORT_MD_CHILD, daughterTemplateNl.vertices());
                IRL_ASSERT(!child_mm.is_null(), "Daughterboard topology {} has no child MD ports.",
                           d_i);

                DaughterMappings dms(m_i, d_i);
                // 5.c: Copy and stitch this
                auto new_nodes_i = out.mNetlist.copy_and_stitch(
                    port, daughterTemplateNl, daughterTemplateNl[daughter_md], &dms);

                out.mBoardSpans.push_back({
                    .boardId = {.mother_id = m_i, .daughter_id = d_i},
                    .span = new_nodes_i,
                });
            }
        }
    }

    return out;
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

} // namespace irl::driver
