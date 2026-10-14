#pragma once

#include <boost/asio.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/outcome/result.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string_view>

#include "core/compiler.hpp"
#include "core/route.hpp"

typedef enum ProgrammingErrorKind {
    PORT_CLOSED,
    TIMEOUT_HIT,
    BAD_RESPONSE,
    ERROR_CODE,
    OTHER_SYSTEM,
} ProgrammingErrorKind;

typedef struct success_t {
} success_t;

class ProgrammingError : public std::runtime_error {
    ProgrammingError(ProgrammingErrorKind kind, std::string str)
        : kind(kind), std::runtime_error(str) {};

  public:
    ProgrammingErrorKind kind;

    using Result = boost::outcome_v2::result<success_t, ProgrammingError>;

    static ProgrammingError bad_response(uint8_t const *expected, size_t e_n, uint8_t const *got,
                                         size_t g_n);

    static inline ProgrammingError port_closed() {
        return ProgrammingError(PORT_CLOSED, "Port closed by peer");
    };

    static inline ProgrammingError timeout_hit() {
        return ProgrammingError(TIMEOUT_HIT, "Timeout hit");
    };

    static inline ProgrammingError error_code() {
        return ProgrammingError(ERROR_CODE, "Error code received");
    };

    ProgrammingError(boost::system::error_code ec)
        : kind(OTHER_SYSTEM), std::runtime_error("System error: " + ec.message()) {}
};

class SerialWrapper {
  private:
    class context_t {
      public:
        boost::asio::io_context io;
        boost::asio::serial_port port;
        uint64_t timeout_ms;

        context_t(std::string_view name, uint64_t timeout_ms)
            : io(), port(boost::asio::serial_port(io, name.data())), timeout_ms(timeout_ms) {};
    };
    std::optional<context_t> context;
    std::ostream &log_fd;
    bool do_log;

  public:
    SerialWrapper(std::ostream &log_fd) : log_fd(log_fd), context(std::nullopt), do_log(true) {};
    SerialWrapper(bool do_log, std::ostream &log_fd, std::string_view serial_port,
                  uint64_t timeout_ms, size_t baud);

    ProgrammingError::Result send(boost::asio::const_buffer const &buffer);
    // Saves contents of buffer into a temp, then reads into buffer
    // Returns success iff buffer matches temp
    ProgrammingError::Result read_expecting(boost::asio::mutable_buffer const &buffer);
};

class TspiceProgrammer {
  public:
  private:
    std::unique_ptr<SerialWrapper> serial;
    IrlCompiler const &compiler;

    bool send_w_timeout();

  public:
    // Constructors
    TspiceProgrammer(std::unique_ptr<SerialWrapper> serial, IrlCompiler const &compiler);

    // Pings to see if board connected
    ProgrammingError::Result ping_board();
    // Resets the board.
    ProgrammingError::Result try_reset_board();
    // Sends stream of programming commands to the board, with timeouts
    ProgrammingError::Result send_stream(ProgrammingInfo const &prog_info);

    // Sends a worst-case timing event (sends all connections + bruzzes)
    ProgrammingError::Result send_worstcase(SimpleTspiceInfo const &board);
};
