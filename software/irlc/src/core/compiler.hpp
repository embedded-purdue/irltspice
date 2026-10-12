#pragma once

#include <array>
#include <optional>
#include <ostream>
#include <string>

enum IrlCompilerOptionGroup {
    GENERAL,
    PARSING,
    ROUTING,
    PROGRAMMING,
    INFO,
};
static std::array IRL_COMPILER_OPTION_GROUPS = {GENERAL, PARSING, ROUTING, PROGRAMMING, INFO};
static inline const char *ilrCompilerOptionGroupName(IrlCompilerOptionGroup o) {
    switch (o) {
    case GENERAL:
        return "General";
        break;
    case PARSING:
        return "Netlist Parsing";
        break;
    case ROUTING:
        return "TSPICE Routing";
        break;
    case PROGRAMMING:
        return "TSPICE Programming";
        break;
    case INFO:
        return "Info Displaying";
        break;
    }
}

#define INFILE_OPT input_file

typedef enum unspecified_t { UNSPECIFIED } unspecified_t;

#undef FLAG_OPT
#undef TYPED_OPT
#define COMPILER_OPTIONS                                                                           \
    FLAG_OPT(help, "h", "Show this help message and exit", GENERAL)                                \
    FLAG_OPT(version, "", "Print version and exit", GENERAL)                                       \
    TYPED_OPT(INFILE_OPT, "", std::optional<std::string>, std::nullopt,                            \
              "Input spice file to compile", GENERAL)                                              \
    FLAG_OPT(do_timing, "", "Time all stages and print results", GENERAL)                          \
    FLAG_OPT(show_components, "", "List the references + values of recognized components", INFO)   \
    FLAG_OPT(show_nets, "", "List special nets and their recognized names", INFO)                  \
    FLAG_OPT(verbose_lex, "", "Enable verbose netlist lexing (tokenizing)", PARSING)               \
    FLAG_OPT(verbose_parse, "", "Enable verbose netlist parsing", PARSING)                         \
    FLAG_OPT(verbose_final_netlist, "", "Log final parsed netlist", PARSING)                       \
    FLAG_OPT(verbose_cell_assign, "", "Enable verbose cell assignment", ROUTING)                   \
    FLAG_OPT(verbose_verify, "", "Enable verbose rule verification", ROUTING)                      \
    FLAG_OPT(verbose_connections, "", "Enable verbose connection building", ROUTING)               \
    FLAG_OPT(verbose_program, "", "Enable verbose board progamming", PROGRAMMING)                  \
    FLAG_OPT(do_programming, "p",                                                                  \
             "Enable board programming. Must be used with -m and/or -s. Default is false",         \
             PROGRAMMING)                                                                          \
    FLAG_OPT(print_serial, "m", "Should print programming output", PROGRAMMING)                    \
    TYPED_OPT(serial_port, "s", std::optional<std::string>, std::nullopt, "Serial port of TSPICE", \
              PROGRAMMING)                                                                         \
    TYPED_OPT(serial_baud, "b", size_t, 115200, "Baudrate of TSPICE (default = 115200)",           \
              PROGRAMMING)                                                                         \
    TYPED_OPT(serial_timeout, "", size_t, 100, "Timeout (ms) for board (default = 100)",           \
              PROGRAMMING)                                                                         \
    TYPED_OPT(board, "", std::string, "MAIN_BOARD", "Board to use", PROGRAMMING)                   \
    FLAG_OPT(do_worstcase, "", "Do worst case and time it. NOT FOR NORMAL USERS", PROGRAMMING)     \
    FLAG_OPT(expect_dump, "", "Expect a dump at end of programming", PROGRAMMING)                  \
    FLAG_OPT(verbose, "v", "SUPER VERBOSE. Enable all verbose flags", GENERAL)

struct IrlCompilerOptions {
#define FLAG_OPT(LONG_N, SHORT_N, MSG, CATEGORY) bool LONG_N;
#define TYPED_OPT(LONG_N, SHORT_N, TYPE, DEFAULT, MSG, CATEGORY) TYPE LONG_N;

    COMPILER_OPTIONS

#undef FLAG_OPT
#undef TYPED_OPT

#define SHOULD_VERBOSE(THING)                                                                      \
    inline bool should_verbose_##THING() const { return verbose || verbose_##THING; };

    SHOULD_VERBOSE(lex)
    SHOULD_VERBOSE(parse)
    SHOULD_VERBOSE(final_netlist)
    SHOULD_VERBOSE(cell_assign)
    SHOULD_VERBOSE(verify)
    SHOULD_VERBOSE(connections)
    SHOULD_VERBOSE(program)
};

class IrlCompiler {
  public:
    IrlCompilerOptions opts;
    std::ostream &log_fd;

    IrlCompiler(IrlCompilerOptions &&opts, std::ostream &log_fd) : opts(opts), log_fd(log_fd) {}

    // Run the compiler.  Does everything.
    // All other functions are a subset of this function.
    // Returns a return code
    int invoke();

    // Prints all messages demanded by options
    // Returns the number of messages printed
    int print_info();

    // Helpers
    inline bool verbose() const { return opts.verbose; };

    inline void log_error(std::string const &msg) { log_fd << "[ERROR] - " << msg << "\n"; }
};

#define QUOTE(X) #X
#define EX_AND_QUOTE(X) QUOTE(X)
