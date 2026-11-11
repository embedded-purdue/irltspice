#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <ratio>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "irl/command.hpp"
#include "irl/detail/util.hpp"
#include "irl/driver/framing.h"
#include "irl/driver/spsc_queue.hpp"
#include "irl/driver/translate_protobuf.hpp"
#include "serialib.h"

namespace irl::driver {

#define POLL_INCOMING_MS 20

enum DriverCode {
    OK,
    SERIAL_CLOSED,
    WRITE_FAILED,
    DEVICE_NOT_FOUND,
    FAILED_TO_SERIALIZE,
};

struct DriverCmdResult {
    size_t cmd_no;
    DriverCode code;
};

struct DriverCmd {
    struct DisconnectSerial {};
    struct ConnectSerial {
        std::string port;
    };

    size_t cmd_no;
    std::variant<MotherCommandPB, DisconnectSerial, ConnectSerial> cmd;
};

// Shared data between main and the worker thread.
// Owned by main thread.
struct IoThreadShared {
    // Updated whenever we receive a new topology.
    // Generally changes to this will result in a new message in the queue
    std::atomic<size_t> aTopologySequenceNo = std::random_device{}();

    std::atomic<bool> aKnownConnected = false;
    // std::atomic<std::string*> knownConnectedPort;

    // Queue of new topologies coming to main thread.
    // .second is sequence number associated with them
    SpscQueue<std::pair<TopologyResponsePB, size_t>> topologiesToMain;
    // Queue of other board responses coming to main thread.
    // Will not hold TopologyResponsePB
    SpscQueue<CommandResponsePB> boardResponsesToMain;
    // Queue of driver command responses coming to main
    SpscQueue<DriverCmdResult> driverResponsesToMain;
    // Outgoing commands
    SpscQueue<DriverCmd> outgoingCmdsToIo;
};

//==========================
// BEGIN WORKER THREAD CODE
//==========================
//
void run_io_thread(std::weak_ptr<IoThreadShared> ctx);

// The actual worker thread
class IoThreadWorker {
    std::shared_ptr<IoThreadShared> mCtx;
    std::weak_ptr<IoThreadShared> mCtx_w;
    serialib mSerial;
    // Bytes pulled off the wire, plus the framing parser that consumes them.
    std::vector<uint8_t> mRxBuffer;
    FrameParser mFrameParser;

    IoThreadWorker(std::weak_ptr<IoThreadShared> ctx);

    // Worker thread entrypoint. ONLY CALL ON WORKER THREAD
    void main();
    // Constructor. ONLY CALL ON WORKER THREAD
    // Destructor. ONLY CALL ON WORKER THREAD
    ~IoThreadWorker();

    // std::

    DriverCode run_cmd(MotherCommandPB const &cmd);
    DriverCode run_cmd(DriverCmd::DisconnectSerial const &disconnect);
    DriverCode run_cmd(DriverCmd::ConnectSerial const &connect);

    DriverCode serialize_and_send(::google::protobuf::Message const *msg);

    // Drain the serial receive buffer and hand every complete frame to main.
    void poll_serial();
    // Decode the frame currently held by mFrameParser and queue it up.
    void process_frame();

    bool is_connected();
    void disconnect();

    friend void run_io_thread(std::weak_ptr<IoThreadShared> ctx);
};

//========================
// END WORKER THREAD CODE
//========================

//========================
// BEGIN MAIN THREAD CODE
//========================

// An RAII-based IO thread.
// Creating this spawns a thread, destroying it signals it to stop and joins it (may take time).
// All public functions are ran on main thread, all private are on the worker thread
class IoThread {
    std::shared_ptr<IoThreadShared> mCtx;
    std::thread mThread;
    size_t cmd_no;

  public:
    IoThread() : mCtx(new IoThreadShared()), mThread(&run_io_thread, std::weak_ptr(mCtx)) {}

    inline size_t get_topo_sequence_no() {
        return mCtx->aTopologySequenceNo.load(std::memory_order_relaxed);
    }
    inline bool get_connected() { return mCtx->aKnownConnected.load(std::memory_order_relaxed); }

    inline SpscQueue<std::pair<TopologyResponsePB, size_t>> &topologies_to_main() {
        return mCtx->topologiesToMain;
    }

    inline SpscQueue<CommandResponsePB> &other_resps_to_main() {
        return mCtx->boardResponsesToMain;
    }

    // Responses:
    //  OK - ok
    inline size_t send_disconnect() {
        cmd_no++;
        mCtx->outgoingCmdsToIo.push(
            std::make_unique<DriverCmd>(cmd_no, DriverCmd::DisconnectSerial{}));
        return cmd_no;
    }

    // Responses:
    //  OK - ok
    //  DEVICE_NOT_FOUND - Could not find serial port
    //  SERIAL_CLOSED - Other error
    inline size_t send_connect(std::string_view stview) {
        cmd_no++;
        mCtx->outgoingCmdsToIo.push(
            std::make_unique<DriverCmd>(cmd_no, DriverCmd::ConnectSerial{std::string(stview)}));
        return cmd_no;
    }

    // Responses:
    //  OK - ok
    //  SERIAL_CLOSED - port not connected
    //  FAILED_TO_SERIALIZE - couldn't serialize protobuf
    //  WRITE_FAILED - failed to write the bytes to the serial device
    inline size_t send_cmd(MotherCommandPB &&cmd) {
        cmd_no++;
        mCtx->outgoingCmdsToIo.push(std::make_unique<DriverCmd>(cmd_no, std::move(cmd)));
        return cmd_no;
    }

    inline std::optional<DriverCode> await_cmd(size_t cmd_no, std::chrono::milliseconds ms) {
        auto start = std::chrono::steady_clock::now();
        std::unique_ptr<DriverCmdResult> resp = nullptr;
        auto now = std::chrono::steady_clock::now();
        // Spin
        while (now - start < ms && !(resp && resp->cmd_no == cmd_no)) {
            resp = mCtx->driverResponsesToMain.try_pop();
            now = std::chrono::steady_clock::now();
        }
        if (resp && resp->cmd_no == cmd_no) {
            return resp->code;
        } else {
            return std::nullopt;
        }
    }

    ~IoThread() {
        // Dropping the shared state makes LOCK() fail so the worker returns.
        // We must then join it: ~std::thread on a joinable thread terminates.
        mCtx.reset();
        if (mThread.joinable()) {
            mThread.join();
        }
    }
};

} // namespace irl::driver
