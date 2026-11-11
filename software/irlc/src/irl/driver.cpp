#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <variant>
#include <vector>

#include <irl/driver.hpp>

#include "command.pb.h"
#include "irl/command.hpp"
#include "irl/detail/util.hpp"
#include "irl/driver/io_thread.hpp"
#include "irl/driver/topology_db.hpp"
#include "irl/driver/translate_protobuf.hpp"
#include "irl/netlist.hpp"

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

    template <typename T> static CommandResult<T> timeout_rx() {
        return CommandResult<T>(
            CommandError{.msg = "Timeout has been hit while trying to receive command.",
                         .code = CommandErrorCode::TIMEOUT_RX});
    }

    template <typename T> static CommandResult<T> port_closed() {
        return CommandResult<T>(CommandError{.msg = "Port discovered to be closed.",
                                             .code = CommandErrorCode::PORT_CLOSED});
    }

    template <typename T> static CommandResult<T> cmd_count_mismatch(uint32_t tx, uint32_t rx) {
        return CommandResult<T>(CommandError{
            .msg = std::format("Mismatched sent vs ack-ed commands. Sent: {}, ack-ed: {}", tx, rx),
            .code = CommandErrorCode::PORT_CLOSED});
    }

    template <typename T> static CommandResult<T> tx_failed() {
        return CommandResult<T>(
            CommandError{.msg = "Transmission failed.", .code = CommandErrorCode::TX_FAILED});
    }

    template <typename T> static CommandResult<T> ok(T &&t) { return {std::move(t)}; }

    static CommandResult<NoResponseExpected> good() { return CommandResult(NoResponseExpected{}); }
};

std::ostream &operator<<(std::ostream &os, const CommandError &err) {
    os << std::format("[ERROR, Code: {}, Msg: \"{}\"]", (int)err.code, err.msg);
    return os;
}

IrlDriver::IrlDriver(std::string_view serial_port)
    : mIoThread(std::make_unique<IoThread>()), mTopoDb(std::make_unique<LazyTopologyDB>()) {
    reconnect(serial_port);
}

// Wait for an already sent command get confirmed as transmitted and return an error if it fails
CommandResult<NoResponseExpected> IrlDriver::await_cmd_send(size_t seq_no,
                                                            std::chrono::milliseconds timeout) {
    auto opt = mIoThread->await_cmd(seq_no, 100ms);

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
        IRL_ASSERT(false, "Unreachable code: {} received while tx.", (int)code);
        break;
    }

    return CRF::ok(NoResponseExpected{});
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

    if (!opt) {
        std::cout << "Reconnect failed. No response!\n";
        return false;
    }

    switch (*opt) {
    case OK:
        break;
    case SERIAL_CLOSED:
        return false;
    case DEVICE_NOT_FOUND:
        return false;

    case FAILED_TO_SERIALIZE:
    case WRITE_FAILED:
    default:
        IRL_ASSERT(false, "Unreachable. Response: {} from connect", (int)*opt);
        break;
    }
    return true;
}

CommandResult<NoResponseExpected> IrlDriver::resetTspice() {
    MotherCommandPB reset;
    initResetCmd(&reset);
    std::cout << "Sending reset!\n";
    auto seq = mIoThread->send_cmd(std::move(reset));
    return await_cmd_send(seq);
}

bool IrlDriver::topologyValid(IrlTopology const *topo) {
    size_t seq = mIoThread->get_topo_sequence_no();
    return topo->mTopoSequenceNumber == seq;
}

CommandResult<NoResponseExpected>
IrlDriver::doProgrammingInner(IrlTopology const *topo, CmdIter &cmd_i, CmdIter const &cmd_i_end) {
    if (!topologyValid(topo)) {
        return CRF::staleTopo<NoResponseExpected>();
    }

    auto r = resetTspice();
    if (r.isError()) {
        return r;
    }

    // Collect the commands, by default a command goes to other_cmds unless it is for an spst, in
    // which case it gets combined into spst_commands
    std::unordered_map<uint32_t, ProgramPhysicalSwitches> spst_cmds;
    std::vector<MotherCommandPB> other_cmds;

    for (; cmd_i != cmd_i_end; ++cmd_i) {
        ProgrammingCommand const &cmd = *cmd_i;
        std::cout << "  Got a cmd to send\n";

        if (ProgramLogicalSwitch const *prog = std::get_if<irl::ProgramLogicalSwitch>(&cmd.cmd)) {
            SpstId pid = prog->switch_id;
            pid.sw_id = 0;
            ProgramPhysicalSwitches &pphys = spst_cmds[pid.make_uint()];
            pphys.spst_ic_id = prog->switch_id;
            uint32_t mask = (1 << prog->switch_id.sw_id);
            pphys.active_bitmask |= mask;
            if (prog->connect) {
                pphys.onoff_bitmask |= mask;
            } else {
                pphys.onoff_bitmask &= ~mask;
            }
        } else {
            other_cmds.push_back({});
            initProgrammingCmd(&other_cmds.back(), cmd);
        }
    }

    std::vector<MotherCommandPB> all_cmds{std::move(other_cmds)};

    for (auto const &[_, cmd_sw] : spst_cmds) {
        all_cmds.push_back({});
        ProgrammingCommand cmd{std::move(cmd_sw)};
        initProgrammingCmd(&all_cmds.back(), cmd);
    }

    uint32_t tx_count = 0;
    for (auto &cmd : all_cmds) {
        tx_count++;
        r = sendProgrammingCommandSerialized(topo, std::move(cmd));
        if (r.isError()) {
            return r;
        }
    }

    auto r_uint = endProgramming(topo);
    if (r_uint.isError()) {
        return r_uint.cast_error<NoResponseExpected>();
    }

    uint32_t rx_count = *r_uint.getData();
    if (rx_count != tx_count) {
        return CRF::cmd_count_mismatch<NoResponseExpected>(tx_count, rx_count);
    } else {
        return CRF::ok(NoResponseExpected{});
    }
}

// CommandResult<NoResponseExpected> IrlDriver::beginProgramming(IrlTopology const *topo) {
//     (void)topo;
//     assert(0 && "irl::IrlDriver::beginProgramming is not implemented yet");
//     std::abort();
// }

CommandResult<uint32_t> IrlDriver::endProgramming(IrlTopology const *topo) {
    if (!topologyValid(topo)) {
        return CRF::staleTopo<uint32_t>();
    }
    std::cout << "Sending end!\n";

    MotherCommandPB reset;
    initStopProgrammingCmd(&reset);
    auto seq = mIoThread->send_cmd(std::move(reset));
    auto bad = await_cmd_send(seq);

    if (bad.isError()) {
        return bad.cast_error<uint32_t>();
    }

    std::unique_ptr<CommandResponsePB> resp;
    do {
        resp = mIoThread->other_resps_to_main().try_pop_for_timeout(100ms);
        if (!resp) {
            break;
        } else if (resp->has_stop()) {
            break;
        } else {
            resp = nullptr;
        }
    } while (resp);

    if (resp) {
        return CRF::ok(uint32_t(resp->stop().complete_count()));
    } else {
        return CRF::timeout_rx<uint32_t>();
    }
}

CommandResult<NoResponseExpected> IrlDriver::sendProgrammingCommand(IrlTopology const *topo,
                                                                    ProgrammingCommand const *cmd) {
    MotherCommandPB cmd_serialized{};
    initProgrammingCmd(&cmd_serialized, *cmd);
    return sendProgrammingCommandSerialized(topo, std::move(cmd_serialized));
}

CommandResult<NoResponseExpected>
IrlDriver::sendProgrammingCommandSerialized(IrlTopology const *topo, MotherCommandPB &&cmd) {
    if (!topologyValid(topo)) {
        return CRF::staleTopo<NoResponseExpected>();
    }
    std::cout << "Sending programming!\n";

    auto seq = mIoThread->send_cmd(std::move(cmd));
    return await_cmd_send(seq);
}

CommandResult<IrlTopology> IrlDriver::queryTopology() {
    size_t initial_topo = mIoThread->get_topo_sequence_no();

    MotherCommandPB topo;
    initTopoRequestCmd(&topo);
    auto seq = mIoThread->send_cmd(std::move(topo));
    auto bad = await_cmd_send(seq);

    if (bad.isError()) {
        return bad.cast_error<IrlTopology>();
    }

    std::unique_ptr<std::pair<TopologyResponse, size_t>> resp;
    size_t latest_topo = initial_topo;
    do {
        resp = mIoThread->topologies_to_main().try_pop_for_timeout(500ms);
        latest_topo = mIoThread->get_topo_sequence_no();
        // std::cout << "Got resp! Latest: "<<latest_topo <<" old:"<<initial_topo<<"
        // second:"<<resp->second<<"\n";
        if (resp == nullptr) {
            // Timeout hit. Will detect
            break;
        } else if (resp->second == latest_topo) {
            // we got the most up to date. Exit
            break;
        } else {
            // We got a response but not up to date. Try again
            resp = nullptr;
            continue;
        }
    } while (true);

    if (resp == nullptr) {
        return CRF::timeout_rx<IrlTopology>();
    }

    return CRF::ok(TopoUnpacker::unpackTopology(std::move(resp->first), *mTopoDb, resp->second));
}

IrlDriver::~IrlDriver() = default;

} // namespace irl::driver
