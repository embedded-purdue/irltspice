#include <cassert>
#include <cstdlib>

#include <irl/driver.hpp>

#include "irl/driver/translate_protobuf.hpp"

// Stub implementation for the `interface-refactor`. Every method aborts: the
// real serial-port / protobuf / oscilloscope logic will move here from
// `src/core` (likely under `src/irl/driver/`).
//
// `assert(0)` documents intent in debug builds; `std::abort()` is the
// always-on, noreturn fallback (so non-void stubs do not warn).

namespace irl {

// CRF = Command Result Factory
class CRF {
  public:
    template <typename T> static CommandResult<T> staleTopo() {
        return CommandResult<T>(
            CommandError{.msg = "Board topology has changed. Call queryTopology, make necessary "
                                "changes, and try again.",
                         .code = CommandErrorCode::STALE_TOPOLOGY});
    }
};

IrlDriver::IrlDriver(std::string_view serial_port) {
    (void)serial_port;
    assert(0 && "irl::IrlDriver is not implemented yet");
    std::abort();
}

bool IrlDriver::connected() {
    assert(0 && "irl::IrlDriver::connected is not implemented yet");
    std::abort();
}

void IrlDriver::disconnect() {
    assert(0 && "irl::IrlDriver::disconnect is not implemented yet");
    std::abort();
}

bool IrlDriver::reconnect(std::string_view serial_port) {
    (void)serial_port;
    assert(0 && "irl::IrlDriver::reconnect is not implemented yet");
    std::abort();
}

CommandResult<NoResponseExpected> IrlDriver::resetTspice() {
    assert(0 && "irl::IrlDriver::resetTspice is not implemented yet");
    std::abort();
}
bool IrlDriver::topologyValid(IrlTopology const *topo) {
    // TODO:
    // Check against internal state
    return true;
}

CommandResult<NoResponseExpected> IrlDriver::doProgrammingInner(IrlTopology const *topo,
                                                                CmdCollection const &cmds) {
    if (!topologyValid(topo)) {
        return CRF::staleTopo<NoResponseExpected>();
    }

    auto r = beginProgramming(topo);
    if (r.isError()) {
        return r;
    }

    while (!cmds.cmdsDone()) {
        ProgrammingCommand const &cmd = cmds.currentCmd();
        cmds.nextCmd();
    }
}

CommandResult<NoResponseExpected> IrlDriver::beginProgramming(IrlTopology const *topo) {
    (void)topo;
    assert(0 && "irl::IrlDriver::beginProgramming is not implemented yet");
    std::abort();
}

CommandResult<uint32_t> IrlDriver::endProgramming(IrlTopology const *topo) {
    (void)topo;
    assert(0 && "irl::IrlDriver::endProgramming is not implemented yet");
    std::abort();
}

CommandResult<NoResponseExpected> IrlDriver::sendProgrammingCommand(IrlTopology const *topo,
                                                                    ProgrammingCommand const *cmd) {
    (void)topo;
    (void)cmd;
    assert(0 && "irl::IrlDriver::sendProgrammingCommand is not implemented yet");
    std::abort();
}

CommandResult<IrlTopology> IrlDriver::queryTopology() {
    assert(0 && "irl::IrlDriver::queryTopology is not implemented yet");
    std::abort();
}

} // namespace irl
