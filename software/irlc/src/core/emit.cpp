#include "core/emit.hpp"
#include "core/board_info.hpp"
#include "core/route.hpp"
#include "extern/uart_bytes.h"
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/read.hpp>
#include <boost/system/detail/errc.hpp>

#include <boost/asio.hpp>

#include "command.pb.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

using namespace boost;
using namespace std;
using Result = ProgrammingError::Result;

static inline void send_bytes(ostream &os, uint8_t const *data, size_t n);

TspiceProgrammer::TspiceProgrammer(std::unique_ptr<SerialWrapper> serial,
                                   IrlCompiler const &compiler)
    : serial(std::move(serial)), compiler(compiler) {};

Result TspiceProgrammer::ping_board() {
    // Binary bloat w/ strings
    throw runtime_error("NOT IMPLEMENTED *COUGH* MICAH *COUGH*");
}

Result TspiceProgrammer::send_stream(ProgrammingInfo const &prog_info) {
    // Reset
    Result r = try_reset_board();
    if (!r)
        return r;

    vector<uint8_t> buffer{START_CONFIG, START_CONFIG};

    MotherCommand cmd;
    StopProgramming *toporeq = cmd.mutable_stop();

    std::string str;
    bool good = cmd.SerializeToString(&str);
    if (!good) {
        throw new std::runtime_error("AHHHH");
    }
    r = serial->send(asio::buffer(str.c_str(), str.length()));

    // Send 2 start bytes
    r = serial->send(asio::buffer(buffer));
    if (!r)
        return r;

    buffer.clear();
    buffer.push_back(READY_TO_START);
    r = serial->read_expecting(asio::buffer(buffer));
    buffer.clear();
    if (!r)
        return r;

    for (auto const &resistor : prog_info.resistances) {
        buffer.push_back(init_instr(START_POT, resistor.resistor_id));
        buffer.push_back(resistor.value);
        r = serial->send(asio::buffer(buffer));
        if (!r)
            return r;
        buffer.clear();
        buffer.push_back(RETURN_TO_CONFIG);
        r = serial->read_expecting(asio::buffer(buffer));
        buffer.clear();
        if (!r)
            return r;
    }

    std::unordered_set<decltype(prog_info.connections[0].crossbar_id)> crossbars;

    for (auto connection_begin = prog_info.connections.begin();
         connection_begin != prog_info.connections.end(); ++connection_begin) {
        if (crossbars.contains(connection_begin->crossbar_id)) {
            continue;
        }
        crossbars.insert(connection_begin->crossbar_id);
        buffer.push_back(init_instr(START_CB, connection_begin->crossbar_id));
        for (auto connection = connection_begin; connection != prog_info.connections.end();
             ++connection) {
            if (connection->crossbar_id != connection_begin->crossbar_id)
                continue;
            buffer.push_back(init_connetion(connection->col, connection->row));
        }
        buffer.push_back(END_CB);
        r = serial->send(asio::buffer(buffer));
        if (!r)
            return r;
        buffer.clear();
        buffer.push_back(RETURN_TO_CONFIG);
        r = serial->read_expecting(asio::buffer(buffer));
        buffer.clear();
        if (!r)
            return r;
    }
    // for (auto const &connection : prog_info.connections) {
    //     buffer.push_back(init_instr(START_CB, connection.crossbar_id));
    //     buffer.push_back(init_connetion(connection.col, connection.row));
    //     buffer.push_back(END_CB);
    //     r = serial->send(asio::buffer(buffer));
    //     if (!r)
    //         return r;
    //     buffer.clear();
    //     buffer.push_back(RETURN_TO_CONFIG);
    //     r = serial->read_expecting(asio::buffer(buffer));
    //     buffer.clear();
    //     if (!r)
    //         return r;
    // }

    if (compiler.opts.should_verbose_parse())
        compiler.log_fd << "END!" << std::endl;

    buffer.clear();
    buffer.push_back(END_CONFIG);
    r = serial->send(asio::buffer(buffer));
    if (!r)
        return r;

    buffer.clear();
    buffer.push_back(CONFIG_SUCCESS);
    if (compiler.opts.expect_dump)
        for (int i = 0; i < 100; i++)
            buffer.push_back(0);
    r = serial->read_expecting(asio::buffer(buffer));
    if (!r)
        return r;

    return success_t{};
}

Result TspiceProgrammer::try_reset_board() {

    if (compiler.opts.should_verbose_program()) {
        compiler.log_fd << "Resetting...";
        flush(compiler.log_fd);
    }

    uint8_t minimal_buf = RESET_CONFIG;

    Result result = serial->send(asio::buffer(&minimal_buf, 1));
    if (!result) {
        if (compiler.opts.should_verbose_program()) {
            compiler.log_fd << "[FAILED]\n";
        }
        return result;
    }
    if (compiler.opts.should_verbose_program()) {
        compiler.log_fd << "sent...";
        flush(compiler.log_fd);
    }

    minimal_buf = RESET_SUCCESS;
    result = serial->read_expecting(asio::buffer(&minimal_buf, 1));
    if (!result) {
        if (compiler.opts.should_verbose_program())
            compiler.log_fd << "[FAILED]\n";
        return result;
    } else {
        if (compiler.opts.should_verbose_program())
            compiler.log_fd << "[OK]\n";
        return success_t{};
    }
}

Result SerialWrapper::send(boost::asio::const_buffer const &buffer) {
    system::error_code error;

    // Sunc send
    if (do_log) {
        log_fd << "Sending: ";
        send_bytes(log_fd, (uint8_t const *)buffer.data(), buffer.size());
        log_fd << "... ";
    }

    if (!context.has_value()) {
        log_fd << "[GOOD(mock)]\n";
        return success_t{};
    }

    context->port.write_some(buffer, error);

    if (error.failed()) {
        if (error == asio::error::eof) {
            if (do_log)
                log_fd << "[FAILED]\n";
            return ProgrammingError::port_closed();
        }
        if (do_log)
            log_fd << "[FAILED]\n";
        return ProgrammingError(error);
    }

    if (do_log)
        log_fd << "[GOOD]\n";
    return success_t{};
}

Result SerialWrapper::read_expecting(boost::asio::mutable_buffer const &buffer) {

    if (do_log) {
        log_fd << "Reading out: ";
        send_bytes(log_fd, (uint8_t *)buffer.data(), buffer.size());
        log_fd << "... ";
    }

    if (!context.has_value()) {
        log_fd << "[GOOD(mock)]\n";
        return success_t{};
    }

    system::error_code error;
    bool read_complete = false;
    size_t read_n = 0;

    // Shoot me in the head VLAs don't exist in C++ so I gotta heap allocate for no reason.
    // Make it static for reuse
    static std::vector<uint8_t> temp_buff;
    temp_buff.assign((uint8_t *)buffer.data(), (uint8_t *)buffer.data() + buffer.size());

    asio::steady_timer timer(context->io, asio::chrono::milliseconds(context->timeout_ms));
    timer.async_wait([&](const system::error_code &ec) {
        if (!ec && !read_complete) {
            context->port.cancel();
        }
    });
    asio::async_read(context->port, buffer, [&](const system::error_code &ec, std::size_t n) {
        error = ec;
        read_n = n;
        if (ec.value() != system::errc::operation_canceled) {
            read_complete = true;
            timer.cancel();
        }
    });

    context->io.run();
    context->io.restart();

    if (do_log) {
        log_fd << "Got (" << read_n << "): ";
        send_bytes(log_fd, (uint8_t *)buffer.data(), read_n);
        log_fd << "... ";
    }

    if (read_complete && !error) {
        if (read_n != buffer.size() ||
            std::memcmp(temp_buff.data(), buffer.data(), buffer.size())) {
            log_fd << "[FAILED]\n";
            return ProgrammingError::bad_response(temp_buff.data(), buffer.size(),
                                                  (uint8_t *)buffer.data(), read_n);
        } else {
            if (do_log)
                log_fd << "[GOOD]\n";
            return success_t{};
        }
    } else if (!read_complete) {
        log_fd << "[FAILED]\n";
        return ProgrammingError::timeout_hit();
    } else {
        log_fd << "[FAILED]\n";
        return ProgrammingError(error);
    }
}

// Error stuff

static inline void send_bytes(ostream &os, uint8_t const *data, size_t n) {
    static char xtbl[] = "0123456789ABCDEF";
    os << "0x";
    if (n != 0) {
        os << xtbl[data[0] >> 4] << xtbl[data[0] & 0xF];
        for (uint8_t const *dp = data + 1; dp != (data + n); dp++) {
            os << "," << xtbl[*dp >> 4] << xtbl[*dp & 0xF];
        }
    }
}

ProgrammingError ProgrammingError::bad_response(uint8_t const *expected, size_t e_n,
                                                uint8_t const *got, size_t g_n) {
    ostringstream ss{};
    assert(e_n != 0);
    ss << "Bad response. Expected: ";
    send_bytes(ss, expected, e_n);
    ss << " and got: ";
    send_bytes(ss, got, g_n);
    return ProgrammingError(BAD_RESPONSE, ss.str());
}

SerialWrapper::SerialWrapper(bool do_log, std::ostream &log_fd, std::string_view serial_port,
                             uint64_t timeout_ms, size_t baud)
    : log_fd(log_fd), do_log(do_log), context(in_place, serial_port, timeout_ms) {

    try {
        context->port.set_option(asio::serial_port::baud_rate(baud));
    } catch (...) {
        throw runtime_error((ostringstream() << "Invalid baud rate: " << baud).str());
    }
    context->port.set_option(asio::serial_port::parity(asio::serial_port::parity::none));
    context->port.set_option(
        asio::serial_port::flow_control(asio::serial_port::flow_control::none));
    context->port.set_option(asio::serial_port::stop_bits(asio::serial_port::stop_bits::one));
}

ProgrammingError::Result TspiceProgrammer::send_worstcase(SimpleTspiceInfo const &board) {
    ProgrammingInfo proginfo;

    auto add_all_conns = [&](PhysCrossbar const &bar) {
        for (int r = 0; r < bar.rows.size(); r++) {
            for (int c = 0; c < bar.cols.size(); c++) {
                proginfo.connections.push_back(CrossbarCon{.crossbar_id = bar.id,
                                                           .row = static_cast<uint8_t>(r),
                                                           .col = static_cast<uint8_t>(c)});
            }
        }
    };

    std::unordered_set<decltype(((ComponentColCon *)0)->id)> r_ids;

    for (auto const &bar : board.root.bars) {
        add_all_conns(bar);
    }
    for (auto const &cell : board.cells) {
        for (auto const &bar : cell.crossbars.bars) {
            add_all_conns(bar);
            for (auto const &col : bar.cols) {
                ComponentColCon const *con = std::get_if<ComponentColCon>(&col);
                if (con && con->kind == R && !r_ids.contains(con->id)) {
                    r_ids.insert(con->id);
                    proginfo.resistances.push_back(QuantizedResistance{
                        .resistor_id = static_cast<uint8_t>(con->id), .value = 0x00});
                }
            }
        }
    }

    auto start = std::chrono::high_resolution_clock::now();

    auto r = this->send_stream(proginfo);

    auto stop = std::chrono::high_resolution_clock::now();

    if (r.has_failure()) {
        return r;
    }

    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
    compiler.log_fd << "Time taken: " << duration.count() << " microseconds" << std::endl;
    return r;
}
