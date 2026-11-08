#include "io_thread.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <variant>

namespace irl::driver {

static void wakeup_thread_main(std::weak_ptr<std::atomic<size_t>> waker) {
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
    frame_parser_init(&mFrameParser, &mRxBuffer);
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
        // Now time to read. Pull whatever bytes have shown up and dispatch every
        // complete frame to the main thread.
        poll_serial();
        UNLOCK();
    }
}

void IoThreadWorker::poll_serial() {
    if (!is_connected()) {
        return;
    }

    // serialib opens the port non-blocking, so a single-byte read returns 1 when
    // a byte is pending and -2 (EAGAIN) once the OS buffer is empty. A 1 ms
    // timeout only matters if read() reports EOF; the buffer is emptied without
    // busy-spinning either way.
    uint8_t byte = 0;
    while (mSerial.readBytes(&byte, 1, 1) == 1) {
        mRxBuffer.push_back(byte);
    }

    // frame_parser_parse() returns true as long as a complete frame is pending,
    // so process_frame() must retire it before we can parse the next one.
    while (frame_parser_parse(&mFrameParser)) {
        process_frame();
    }
}

void IoThreadWorker::process_frame() {
    CommandResponsePB response;
    bool ok = response.ParseFromArray(frame_parser_get_data_start(&mFrameParser),
                                      frame_parser_get_data_length(&mFrameParser));
    // Retire the frame regardless: a malformed payload should just be dropped.
    frame_parser_mark_data_used(&mFrameParser);
    if (!ok) {
        return;
    }

    if (response.has_topo()) {
        size_t seq = mCtx->aTopologySequenceNo.fetch_add(1, std::memory_order_relaxed) + 1;
        mCtx->topologiesToMain.push(
            std::make_unique<std::pair<TopologyResponsePB, size_t>>(response.topo(), seq));
    } else {
        mCtx->boardResponsesToMain.push(std::make_unique<CommandResponsePB>(std::move(response)));
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

DriverCode IoThreadWorker::serialize_and_send(::google::protobuf::Message const *msg) {
    if (!is_connected()) {
        return DriverCode::SERIAL_CLOSED;
    }

    std::vector<uint8_t> payload(msg->ByteSizeLong());
    if (!msg->SerializeToArray(payload.data(), static_cast<int>(payload.size()))) {
        return DriverCode::FAILED_TO_SERIALIZE;
    }

    // frame_payload() prepends the start/length header and appends the CRC.
    std::vector<uint8_t> frame;
    frame_payload(&frame, payload.data(), payload.size());

    int ok = mSerial.writeBytes(frame.data(), static_cast<unsigned int>(frame.size()));
    return ok == 1 ? DriverCode::OK : DriverCode::WRITE_FAILED;
}

} // namespace irl::driver
