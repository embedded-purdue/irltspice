#pragma once

#include <cstdint>
#include <variant>

#include <irl/netlist.hpp>

#include "irl/detail/util.hpp"

namespace irl {

// Program a single crossbar point
struct ProgramXbar {
    // Which crossbar to program
    uint32_t xbarId;
    // Which row to program
    uint32_t row;
    // Which column to program
    uint32_t col;
    // Should this point become connected or disconnected?
    bool connect;
};

// Where to route a programming command
struct CommandDestination {
    // Which motherboard (index into the topology array) to program
    uint32_t motherboardIndex;
    // If programming a daughterboard, which daughterboard index.
    // If not, must be NO_DAUGHTERBOARD, indicating the motherboard as the dest
    uint32_t daughterboardIndex;
    static constexpr uint32_t NO_DAUGHTERBOARD = UINT32_MAX;
};

// A programming command to send to the tspice
struct ProgrammingCommand {
    // Which board to send the command to
    CommandDestination dest;
    // The command to send
    std::variant<ProgramXbar> cmd;
};

// SemVer of the firmware running on a mother/daughter board
struct FirmwareVersion {
    uint16_t major;
    uint8_t minor;
    uint8_t patch;
};

enum class DaughterboardKind : uint32_t {
    // No daughterboard present in this slot
    NOT_PRESENT = 0,
    // The basic, default daughterboard
    BASIC_V1,
    // The testing daughterboard
    TESTING_V1,
    // The diode daughterboard
    DIODE_V1,
    // An unknown kind.
    // Indicates you should update this library
    UNKNOWN = UINT32_MAX,
};

// Topology information about a daughterboard slot
// Could indicate that a daughterboard is not present in this slot
struct DaughterboardTopology {
    // A unique id (serial number) for this specific board
    uint64_t uniqueId;
    FirmwareVersion firmwareVersion;
    DaughterboardKind kind;
    // The actual topology of this daughterboard
    Netlist netlist;
};

// Topology information about a motherboard
struct MotherboardTopology {
    // A unique id (serial number) for this specific board
    uint64_t uniqueId;
    uint32_t hardwareRevision;
    FirmwareVersion firmwareVersion;
    DaughterboardTopology daughterboards[4];
    // The actual topology of this motherboard
    Netlist netlist;
};

// The topology of a connected board.
// Required to perform any programming commands to enforce that the user knows
// the current topology of the board.
// If the tspice signals a topology update to the driver, the driver will
// update its internal topoSequenceNumber rendering any existing "IrlTopology"s
// stale (their sequence number fails to match).
// This enusers any programming code is always working with the most recent
// topology, and will not accidentally damage the tspice.
struct IrlTopology {
    // Array of motherboards with size sizeMotherboards
    std::unique_ptr<MotherboardTopology[]> motherboards;
    size_t sizeMotherboards;

    MotherboardTopology &operator[](size_t i) {
        detail::ASSERT(i < sizeMotherboards, "Index out of bounds");
        return motherboards[i];
    }

  private:
    // Internal detail used to keep track of when this IrlTopology was created.
    // If someone has changed the topology of the tspice (via connecting or
    // disconnecting a board), the driver will detect this and stop accepting
    // this IrlTopology
    size_t topoSequenceNumber;
    // IrlDriver has to be able to check topoSequenceNumber
    friend class IrlDriver;
};

} // namespace irl
