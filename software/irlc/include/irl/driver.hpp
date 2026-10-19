#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

#include <irl/command.hpp>
#include <irl/netlist.hpp>

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

    CommandResult(CommandError &&err) : mInfo(std::move(err)) {};
    CommandResult(Data &&data) : mInfo(std::move(data)) {};

    // A collection of static helper methods to create these results
    friend class CRF;

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

// The class used for interfacing with the tspice
class IrlDriver {

    struct CmdCollection {
        void (*nextCmd)();
        bool (*cmdsDone)();
        ProgrammingCommand const &(*currentCmd)();
    };
    CommandResult<NoResponseExpected> doProgrammingInner(IrlTopology const *topo,
                                                         CmdCollection const &cmds);

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

    bool topologyValid(IrlTopology const *topo);

    // The preferred way to program the board
    // resets, sends begin, sends stream of packets, then sends end
    // Will automatically batch ProgramLogicalSwitch commands
    // command_collection must have begin() and end() which produces iterators
    //   of ilr::ProgrammingCommands
    template <typename T>
    CommandResult<NoResponseExpected> doProgramming(IrlTopology const *topo,
                                                    T const &command_collection) {
        auto current = command_collection.begin();
        auto end = command_collection.end();
        using IterT = decltype(current);
        CmdCollection cmds;
        cmds.nextCmd = [&current]() { (*static_cast<IterT *>(current))++; };
        cmds.cmdsDone = [&current, &end]() {
            return (*static_cast<IterT *>(current)) == (*static_cast<IterT *>(end));
        };
        cmds.currentCmd = [&current]() { return *(*static_cast<IterT *>(current)); };
        return doProgrammingInner(topo, cmds);
    }

    // ADVANCED USE ONLY. Prefer doProgramming
    // Resets the tspice and places it into programming mode.
    // Requires a non-stale topology to ensure no mistakes are made.
    CommandResult<NoResponseExpected> beginProgramming(IrlTopology const *topo);

    // ADVANCED USE ONLY. Prefer doProgramming
    // End programming. Places the tspice in "active" mode.
    // Returns the number of programming commands successfully completed since
    // the last beginProgramming call.
    // Requires a non-stale topology to ensure no mistakes are made.
    CommandResult<uint32_t> endProgramming(IrlTopology const *topo);

    // ADVANCED USE ONLY. Prefer doProgramming
    // Send a programming command to the tspice.
    // It must be in programming mode.
    // Requires a non-stale topology to ensure no mistakes are made.
    CommandResult<NoResponseExpected> sendProgrammingCommand(IrlTopology const *topo,
                                                             ProgrammingCommand const *cmd);
    // Get the topology of the connected tspice
    CommandResult<IrlTopology> queryTopology();
};

} // namespace irl
