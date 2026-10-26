#pragma once

#include <cstdint>
#include <variant>

#include <irl/netlist.hpp>

#include "irl/detail/util.hpp"

namespace irl {

// Program a single crossbar point
struct ProgramXbar {
    // Which crossbar to program
    XbarId xbarId;
    // Which row to program
    uint32_t row;
    // Which column to program
    uint32_t col;
    // Should this point become connected or disconnected?
    bool connect;
};

// Program an individual logical switch (part of a multi-switch IC)
struct ProgramLogicalSwitch {
    // The logical ID of the switch to program
    SpstId switch_id;
    // Should it become connected or disconnected
    bool connect;
};

// ADVANCED USE ONLY
// This command programs different switches depending on its destination
// Program some or all switches on a physical switch IC
struct ProgramPhysicalSwitches {
    // The physical id of the switch IC (local to the board you are sending this command to)
    // The sw_id of this field will be ignored
    SpstId spst_ic_id;
    // Which switches on this IC should be modified by this command
    uint32_t active_bitmask;
    // What state to set each of the switches to (only meaningful for bits where active_bitmask==1)
    // 1 = closed/connected 0 = open/disconnected
    uint32_t onoff_bitmask;
};

// A programming command to send to the tspice
struct ProgrammingCommand {
    // The command to send
    std::variant<ProgramXbar, ProgramLogicalSwitch, ProgramPhysicalSwitches> cmd;
};

// SemVer of the firmware running on a mother/daughter board
struct FirmwareVersion {
    uint16_t major;
    uint8_t minor;
    uint8_t patch;
};

// X-Macro listing the daughterboard kinds (name, wire value). The driver uses
// it to generate the JSON <-> kind mapping, so add new kinds here.
#define IRL_DAUGHTERBOARD_KINDS(X)                                                                 \
    X(NOT_PRESENT, 0)                                                                              \
    X(BASIC_V1, 1)                                                                                 \
    X(TESTING_V1, 2)                                                                               \
    X(DIODE_V1, 3)                                                                                 \
    X(UNKNOWN, UINT32_MAX)

enum class DaughterboardKind : uint32_t {
#define IRL_DAUGHTERBOARD_KIND(NAME, VALUE) NAME = VALUE,
    IRL_DAUGHTERBOARD_KINDS(IRL_DAUGHTERBOARD_KIND)
#undef IRL_DAUGHTERBOARD_KIND
};

// Topology information about a daughterboard slot
// Could indicate that a daughterboard is not present in this slot
struct DaughterboardTopology {
    // A unique id (serial number) for this specific board
    uint64_t uniqueId;
    FirmwareVersion firmwareVersion;
    DaughterboardKind kind;
};

// Topology information about a motherboard
struct MotherboardTopology {
    // A unique id (serial number) for this specific board
    uint64_t uniqueId;
    uint32_t hardwareRevision;
    FirmwareVersion firmwareVersion;
    DaughterboardTopology daughterboards[4];
};

// The topology of a connected board.
// Required to perform any programming commands to enforce that the user knows
// the current topology of the board.
// If the tspice signals a topology update to the driver, the driver will
// update its internal topoSequenceNumber rendering any existing "IrlTopology"s
// stale (their sequence number fails to match).
// This enusers any programming code is always working with the most recent
// topology, and will not accidentally damage the tspice.
class IrlTopology {
    // Array of motherboards with size sizeMotherboards
    MotherboardTopology *mMotherboards;
    size_t mSizeMotherboards;

    Netlist mNetlist;

    // Internal detail used to keep track of when this IrlTopology was created.
    // If someone has changed the topology of the tspice (via connecting or
    // disconnecting a board), the driver will detect this and stop accepting
    // this IrlTopology
    size_t mTopoSequenceNumber;

    // IrlDriver has to be able to check topoSequenceNumber
    friend class IrlDriver;

  public:
    IrlTopology(MotherboardTopology *ms, size_t msz, Netlist &&nets, size_t topo)
        : mMotherboards(ms), mSizeMotherboards(msz), mNetlist(std::move(nets)),
          mTopoSequenceNumber(topo) {};
    ~IrlTopology() { delete[] mMotherboards; }

    Netlist &netlist() { return mNetlist; };

    MotherboardTopology &operator[](size_t i) {
        detail::ASSERT(i < mSizeMotherboards, "Index out of bounds");
        return mMotherboards[i];
    }
};

} // namespace irl
