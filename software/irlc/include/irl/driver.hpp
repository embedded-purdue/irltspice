#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>

#include <irl/command.hpp>
#include <irl/netlist.hpp>

class serialib;

namespace irl::driver {

// An enum identifying the kind of error encountered while sending a command
enum class CommandErrorCode {
    DISCONNECTED,
    STALE_TOPOLOGY,
    PORT_CLOSED,
    TIMEOUT_RX,
    TIMEOUT_TX,
    TX_FAILED,
    CHECKSUM_ERROR,
    OTHER_SYSTEM,
};

// An error resulting from sending a command to the board
struct CommandError {
    std::string msg;
    CommandErrorCode code;
};
inline std::ostream &operator<<(std::ostream &os, const CommandError &err) {
    os << "AHHH";
    return os;
}

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
    Data const *getData() const { return std::get_if<Data>(&mInfo); };
    // gets the error, null if valid data, and points to this object
    CommandError const *getErr() const { return std::get_if<CommandError>(&mInfo); };
};

// Command was issued successfully and no response is expected
struct NoResponseExpected {};

class IoThread;

// The class used for interfacing with the tspice
// The software follows a single-producer single-consumer architecture
// Each connected board gets its own IoThread object, and each IoThread is associated with exactly
// one IrlDriver. The IoThread's lifetime is not necessarily tied to the driver's lifetime, an
// IoThread may spontaneously fall back into an uninitialized state if its port closes, for example.
class IrlDriver {

    struct CmdCollection {
        std::function<void()> nextCmd;
        std::function<bool()> cmdsDone;
        std::function<ProgrammingCommand const &()> currentCmd;
    };
    CommandResult<NoResponseExpected> doProgrammingInner(IrlTopology const *topo,
                                                         CmdCollection const &cmds);

    // An IoThread object is owned by the driver and contains all code needed for synchronization
    // Non-nullable, but thread->alive may become false at any time
    // We're doing a pimpl basically here, but trust it's so much nicer
    std::unique_ptr<IoThread> mIoThread;

  public:
    // Create a driver and bind it to a serial port.
    // If successful, will spawn a new thread to handle background operations
    // (sending/receiving from the scope).
    // If this fails, connected() will return false.
    IrlDriver(std::string_view serial_port);
    ~IrlDriver();

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
        CmdCollection cmds;
        cmds.nextCmd = [&current]() { current++; };
        cmds.cmdsDone = [&current, &end]() { return current == end; };
        cmds.currentCmd = [&current]() {
            if constexpr (std::is_same_v<decltype(*current), ProgrammingCommand>) {
                return *current;
            } else {
                return ProgrammingCommand(*current);
            }
        };
        return doProgrammingInner(topo, cmds);
    }

    // // ADVANCED USE ONLY. Prefer doProgramming
    // // Resets the tspice and places it into programming mode.
    // // Requires a non-stale topology to ensure no mistakes are made.
    // CommandResult<NoResponseExpected> beginProgramming(IrlTopology const *topo);

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

    CommandResult<NoResponseExpected> testSendProg();
};

} // namespace irl::driver
