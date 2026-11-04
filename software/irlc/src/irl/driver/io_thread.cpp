#include "io_thread.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <variant>

#include "irl/owned_span.hpp"

namespace irl::driver {

void wakeup_thread_main(std::weak_ptr<std::atomic<size_t>> waker) {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INCOMING_MS));
        auto locked = waker.lock();
        if (locked) {
            locked->fetch_add(1, std::memory_order_relaxed);
        } else {
            // other stuff has been free-ed. We done
            break;
        }
    }
}
#define LOCK()                                                                                     \
    do {                                                                                           \
        mCtx = mCtx_w.lock();                                                                      \
        if (!mCtx)                                                                                 \
            return;                                                                                \
    } while (0);
#define UNLOCK()                                                                                   \
    do {                                                                                           \
        mCtx.reset();                                                                              \
        if (mCtx_w.expired())                                                                      \
            return;                                                                                \
    } while (0);

IoThreadWorker::IoThreadWorker(std::weak_ptr<IoThreadShared> ctx)
    : mCtx(nullptr), mCtx_w(ctx), mSerial() {
    // Thread-ception.
    // Start a waker thread that will wake this thread so we can poll for serial every n ms
    LOCK();
    std::thread(wakeup_thread_main, mCtx->outgoingCmdsToIo.get_waker_token()).detach();
    UNLOCK();
    // Don't have to worry about joining bc eventually when stuff
}

extern void run_io_thread(std::weak_ptr<IoThreadShared> ctx) {
    // Create and run
    IoThreadWorker{ctx}.main();
}

void IoThreadWorker::main() {
    std::shared_ptr<IoThreadShared> mCtx;
    while (true) {
        // Look for incoming commands, eventually we'll get woken up but that's fine
        // If we have them, send them
        LOCK();
        std::unique_ptr<DriverCmd> cmd = mCtx->outgoingCmdsToIo.pop_or_wake();
        for (; cmd; cmd = mCtx->outgoingCmdsToIo.try_pop()) {
            DriverCode result = std::visit([&](auto const &c) { return run_cmd(c); }, cmd->cmd);
            mCtx->driverResponsesToMain.push(
                std::make_unique<DriverCmdResult>(cmd->cmd_no, result));
        }
        UNLOCK();
        // Now time to read
        // TODO:
        // Parse frame, if it's a TopologyResponsePB queue it up in topologiesToMain else queue it
        // up boardResponsesToMain
    }
}

bool IoThreadWorker::is_connected() {
    bool out = mSerial.isDeviceOpen();
    if (out != mCtx->aKnownConnected.load(std::memory_order_relaxed)) {
        mCtx->aKnownConnected.store(out, std::memory_order_relaxed);
    }
    return out;
}
void IoThreadWorker::disconnect() {
    if (mSerial.isDeviceOpen()) {
        mSerial.closeDevice();
    }
    mCtx->aKnownConnected.store(false, std::memory_order_relaxed);
}

DriverCode IoThreadWorker::run_cmd(MotherCommandPB const &cmd) { return serialize_and_send(&cmd); }
DriverCode IoThreadWorker::run_cmd(DriverCmd::DisconnectSerial const &disconnect_cmd) {
    disconnect();
    return OK;
}

DriverCode IoThreadWorker::run_cmd(DriverCmd::ConectSerial const &connect_cmd) {
    disconnect();
    char result = mSerial.openDevice(connect_cmd.port.c_str(), 115200);
    if (result == 1 && is_connected()) {
        return OK;
    } else if (result == -1) {
        return DEVICE_NOT_FOUND;
    } else {
        return SERIAL_CLOSED;
    }
}

IoThreadWorker::~IoThreadWorker() { mSerial.closeDevice(); }

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

static OwnedSpan<uint8_t> serialize_and_frame(::google::protobuf::Message const *msg) {
    size_t payload_size = msg->ByteSizeLong();
    size_t msg_size = HEADER_LEN_BYTES + payload_size + CRC_LEN_BYTES;

    auto data = new uint8_t[msg_size];
    uint8_t *data_iter = data;
    frame_header(&data_iter, payload_size);
    bool good = msg->SerializeToArray(data_iter, msg_size);
    if (!good) {
        return OwnedSpan<uint8_t>(nullptr, 0);
    }
    frame_crc(&data_iter, 0);
    return OwnedSpan(data, msg_size);
}

DriverCode IoThreadWorker::serialize_and_send(::google::protobuf::Message const *msg) {
    OwnedSpan data = serialize_and_frame(msg);
    if (data.data() == nullptr) {
        return DriverCode::FAILED_TO_SERIALIZE;
    }
    if (!mSerial.isDeviceOpen()) {
        return DriverCode::SERIAL_CLOSED;
    }
    int ok = mSerial.writeBytes(data.data(), data.size());
    if (ok == 1) {
        return DriverCode::OK;
    } else {
        return DriverCode::WRITE_FAILED;
    }
}

} // namespace irl::driver
