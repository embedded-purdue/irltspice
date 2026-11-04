#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>

#include <irl/driver.hpp>

#include "irl/detail/util.hpp"
#include "irl/driver/io_thread.hpp"
#include "irl/driver/translate_protobuf.hpp"
#include "irl/netlist.hpp"
#include "serialib.h"

namespace irl::driver {

using namespace std::chrono_literals;

// CRF = Command Result Factory
class CRF {
  public:
    template <typename T> static CommandResult<T> staleTopo() {
        return CommandResult<T>(
            CommandError{.msg = "Board topology has changed. Call queryTopology, make necessary "
                                "changes, and try again.",
                         .code = CommandErrorCode::STALE_TOPOLOGY});
    }

    template <typename T> static CommandResult<T> timeout_tx() {
        return CommandResult<T>(
            CommandError{.msg = "Timeout has been hit while trying to send command.",
                         .code = CommandErrorCode::TIMEOUT_TX});
    }

    template <typename T> static CommandResult<T> port_closed() {
        return CommandResult<T>(CommandError{.msg = "Port discovered to be closed.",
                                             .code = CommandErrorCode::PORT_CLOSED});
    }

    template <typename T> static CommandResult<T> tx_failed() {
        return CommandResult<T>(
            CommandError{.msg = "Transmission failed.", .code = CommandErrorCode::TX_FAILED});
    }

    template <typename T> static CommandResult<T> ok(T &&t) { return {std::move(t)}; }

    static CommandResult<NoResponseExpected> good() { return CommandResult(NoResponseExpected{}); }
};

IrlDriver::IrlDriver(std::string_view serial_port) {
    mIoThread = std::make_unique<IoThread>();
    reconnect(serial_port);
}

bool IrlDriver::connected() { return mIoThread->get_connected(); }

void IrlDriver::disconnect() {
    auto seq = mIoThread->send_disconnect();
    auto opt = mIoThread->await_cmd(seq, 100ms);
    IRL_ASSERT(opt && *opt == OK, "Failed to disconnect!");
}

bool IrlDriver::reconnect(std::string_view serial_port) {
    auto seq = mIoThread->send_connect(serial_port);
    auto opt = mIoThread->await_cmd(seq, 100ms);
    return opt && *opt == OK;
}

CommandResult<NoResponseExpected> IrlDriver::resetTspice() {
    MotherCommandPB reset;
    reset.mutable_reset();
    auto seq = mIoThread->send_cmd(std::move(reset));
    auto opt = mIoThread->await_cmd(seq, 100ms);
    if (!opt) {
        return CRF::timeout_tx<NoResponseExpected>();
    }
    DriverCode code = *opt;
    switch (code) {
    case OK:
        break;
    case SERIAL_CLOSED:
        return CRF::port_closed<NoResponseExpected>();
        break;
    case WRITE_FAILED:
        return CRF::tx_failed<NoResponseExpected>();
        break;
    case FAILED_TO_SERIALIZE:
        IRL_ASSERT(false, "Failed to serialize the message");
        break;
    case DEVICE_NOT_FOUND:
    default:
        IRL_ASSERT(false, "Unreachable code: {} received.", (int)code);
        break;
    }
    return CRF::ok(NoResponseExpected{});
}

bool IrlDriver::topologyValid(IrlTopology const *topo) {
    size_t seq = mIoThread->get_topo_sequence_no();
    return topo->mTopoSequenceNumber == seq;
}

CommandResult<NoResponseExpected> IrlDriver::doProgrammingInner(IrlTopology const *topo,
                                                                CmdCollection const &cmds) {
    if (!topologyValid(topo)) {
        return CRF::staleTopo<NoResponseExpected>();
    }

    auto r = resetTspice();
    if (r.isError()) {
        return r;
    }

    while (!cmds.cmdsDone()) {
        ProgrammingCommand const &cmd = cmds.currentCmd();
        cmds.nextCmd();
    }
    assert(0 && "irl::IrlDriver::resetTspice is not implemented yet");
}

// CommandResult<NoResponseExpected> IrlDriver::beginProgramming(IrlTopology const *topo) {
//     (void)topo;
//     assert(0 && "irl::IrlDriver::beginProgramming is not implemented yet");
//     std::abort();
// }

CommandResult<uint32_t> IrlDriver::endProgramming(IrlTopology const *topo) {
    MotherCommandPB reset;
    reset.mutable_stop();
    auto seq = mIoThread->send_cmd(std::move(reset));
    auto opt = mIoThread->await_cmd(seq, 100ms);
    if (!opt) {
        return CRF::timeout_tx<uint32_t>();
    }
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

IrlDriver::~IrlDriver() = default;

} // namespace irl::driver
