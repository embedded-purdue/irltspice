#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>

#include <irl/driver.hpp>

#include "irl/detail/util.hpp"
#include "irl/driver/translate_protobuf.hpp"
#include "irl/netlist.hpp"
#include "serialib.h"

namespace irl::driver {

// CRF = Command Result Factory
class CRF {
  public:
    template <typename T> static CommandResult<T> staleTopo() {
        return CommandResult<T>(
            CommandError{.msg = "Board topology has changed. Call queryTopology, make necessary "
                                "changes, and try again.",
                         .code = CommandErrorCode::STALE_TOPOLOGY});
    }

    static CommandResult<NoResponseExpected> good() { return CommandResult(NoResponseExpected{}); }
};

// TODO:
// FIX TF OUTTA THIS
#define START_BYTE 0xAB
#define HEADER_LEN_BYTES 5
#define CRC_LEN_BYTES 2
static void frame_header(uint8_t **b, size_t payload_len) {
    auto begin = *b;
    *((*b)++) = START_BYTE;
    *((*b)++) = (payload_len & 0xFF00) >> 8;
    *((*b)++) = payload_len & 0xFF;
    *((*b)++) = (payload_len & 0xFF00) >> 8;
    *((*b)++) = payload_len & 0xFF;
    auto diff = *b - begin;
    IRL_ASSERT(diff == HEADER_LEN_BYTES, "Bad amt written");
}

static void frame_crc(uint8_t **b, uint16_t crc) {
    // crc - not yet implemented
    auto begin = *b;
    *((*b)++) = (crc & 0xFF00) >> 8;
    *((*b)++) = crc & 0xFF;
    auto diff = *b - begin;
    IRL_ASSERT(diff == CRC_LEN_BYTES, "Bad amt written");
}

struct BufferSized {
    std::unique_ptr<uint8_t[]> data;
    size_t len;
};

static BufferSized serialize_and_frame(::google::protobuf::Message const *msg) {
    size_t payload_size = msg->ByteSizeLong();
    size_t msg_size = HEADER_LEN_BYTES + payload_size + CRC_LEN_BYTES;

    auto data = std::make_unique<uint8_t[]>(msg_size);
    uint8_t *data_iter = data.get();
    frame_header(&data_iter, payload_size);
    bool good = msg->SerializeToArray(data_iter, msg_size);
    IRL_ASSERT(good, "Failed to serialize");
    frame_crc(&data_iter, 0);
    return {
        .data = std::move(data),
        .len = msg_size,
    };
}

static bool open_good_settings(serialib *serial, std::string_view serial_port) {
    std::string str{serial_port};
    serial->openDevice(str.c_str(), 115200);
    return serial->isDeviceOpen();
}

IrlDriver::IrlDriver(std::string_view serial_port) {
    mSerial = new serialib;
    open_good_settings(mSerial, serial_port);
    IRL_ASSERT(mSerial->isDeviceOpen(), "Failed to open: {}", serial_port);
}

bool IrlDriver::connected() { return mSerial->isDeviceOpen(); }

void IrlDriver::disconnect() { mSerial->closeDevice(); }

bool IrlDriver::reconnect(std::string_view serial_port) {
    if (connected()) {
        disconnect();
    }
    open_good_settings(mSerial, serial_port);
    IRL_ASSERT(mSerial->isDeviceOpen(), "Failed to open: {}", serial_port);
}

CommandResult<NoResponseExpected> IrlDriver::resetTspice() {
    // MotherCommandPB reset;
    // reset.mutable_
    // serialize_and_frame(ProgrammingCommandPB{})
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
    assert(0 && "irl::IrlDriver::resetTspice is not implemented yet");
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

CommandResult<NoResponseExpected> IrlDriver::testSendProg() {
    ProgrammingCommand pcmd;
    pcmd.cmd = ProgramXbar{
        .xbarId =
            {
                .ic_id = 0,
                .board_id =
                    {
                        .mother_id = 0,
                        .daughter_id = NO_DAUGHTERBOARD,
                    },
            },
        .row = 2,
        .col = 1,
        .connect = true,
    };

    MotherCommandPB mcmd;
    initProgrammingCmd(&mcmd, pcmd);

    auto data = serialize_and_frame(&mcmd);

    uint32_t nwrit;
    int good = mSerial->writeBytes(data.data.get(), data.len, &nwrit);
    IRL_ASSERT(good == 1, "Bad response: {}", good);
    IRL_ASSERT(nwrit == data.len, "Bad len written: {}", nwrit);
    return CRF::good();
}

} // namespace irl::driver
