#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include "irl_netlist.hpp"

namespace irl {

// An enum identifying the kind of error encountered while sending a command
enum class CommandErrorCode {
    DISCONNECTED,
    STALE_TOPOLOGY,
    PORT_CLOSED,
    TIMEOUT_HIT,
    CHECKSUM_ERROR,
    OTHER_SYSTEM,
};

// An error resulting from sending a command to the board
struct CommandError {
    std::string msg;
    CommandErrorCode code;
};

// The result of sending a command to the tspice
// Either an error or the response
template <typename Data> class CommandResult {
  private:
    std::variant<CommandError, Data> mInfo;
    // A bunch of static helper methods to create these results
    friend class CommandResultFactory;

  public:
    // Did this command result in an error?
    // If true, getData() = nullptr
    bool isError() const { return std::holds_alternative<CommandError>(mInfo); }

    // gets the data, null if an error, and points to this object
    Data const *getData() const { return std::get_if<Data>(mInfo); };
    // gets the error, null if valid data, and points to this object
    CommandError const *getErr() const { return std::get_if<CommandError>(mInfo); };
};

// Command was issued successfully and no response is expected
struct NoResponseExpected {};

// Program a crossbar on the given board
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
    // If programming a daughterboard, which daughterboard index
    // If not, must be NO_DAUGHTERBOARD, indicating the motherboard as the dest
    uint32_t daughterboardIndex;
    static constexpr uint32_t NO_DAUGHTERBOARD = UINT32_MAX;
};

// A ptrogramming command to send to the tspice
struct ProgrammingCommand {
    // Which board to send the command to
    CommandDestination dest;
    // The command to send
    std::variant<ProgramXbar> cmd;
};

// SemVer of the frimware running on a mother/daughter board
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
    // A unique id (serial number) for this sepcific board
    uint64_t uniqueId;
    FirmwareVersion firmwareVersion;
    DaughterboardKind kind;
    // The actual topology of this daughterboard
    Netlist netlist;
};

// Topology information aobut a motherboard
struct MotherboardTopology {
    // A unique id (serial number) for this sepcific board
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

  private:
    // Internal detail used to keep track of when this IrlTopology was created.
    // If someone has changed the topology of the tspice (via connecting or
    // disconnecting a board), the driver will detect this and stop accepting
    // this IrlTopology
    size_t topoSequenceNumber;
    // IrlDriver has to be able to check topoSequenceNumber
    friend class IrlDriver;
};

// The class used for interfacing with the tspice
class IrlDriver {
  public:
    // Create a driver and bind it to a serial port.
    // If successful, will spawn a new thread to handle background operations
    // (sending/receiving from the scope).
    // If this fails, connected() will return false.
    IrlDriver(std::string_view serial_port);

    // Is the driver connected to the serial port?
    // If false, all future commands will return DISCONNECTED
    bool connected();

    // Disconnects from the current serial port.
    // Implicitly called by destructor.
    void disconnect();

    // Attempts to reconnect to a serial port.
    // Implicitly calls disconnect()
    // Returns true on success, false on failure
    bool reconnect(std::string_view serial_port);

    // Reset the tspice and places it into idle mode.
    // Disconnects all existing connections and stops any ongoing functionality.
    CommandResult<NoResponseExpected> resetTspice();

    // Resets the tspice and places it into programming mode.
    // Requires a non-stale toplogy to ensure no mistakes are made.
    CommandResult<NoResponseExpected> beginProgramming(IrlTopology const *topo);

    // End programming. Places the tspice in "active" mode.
    // Returns the number of programming commands successfully completed since
    // the last beginProgramming call.
    // Requires a non-stale toplogy to ensure no mistakes are made.
    CommandResult<uint32_t> endProgramming(IrlTopology const *topo);

    // Send a programming command to the tspice.
    // It must be in programming mode.
    // Requires a non-stale toplogy to ensure no mistakes are made.
    CommandResult<NoResponseExpected> sendProgrammingCommand(IrlTopology const *topo,
                                                             ProgrammingCommand const *cmd);

    // Get the topology of the connected tspice
    CommandResult<IrlTopology> queryTopology();
};

} // namespace irl
