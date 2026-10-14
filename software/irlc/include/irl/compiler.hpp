#pragma once

#include <iostream>
#include <memory>
#include <ostream>
#include <string_view>
#include <vector>

#include <irl/command.hpp>
#include <irl/netlist.hpp>

namespace irl {

struct CompilerOptions {
    bool should_verbose_parse = true;
    bool should_verbose_lex = true;
    std::ostream &log_fd = std::cout;
};

// Data about a failure to parse
struct ParseError {
    // Error message
    std::string message;
    // A pointer into file_contents outlining the problematic region
    // Could be a name or a whole line or a series of lines
    std::string_view problematic_region;
    // The file_name for which this error occurred
    std::string_view file_name;
    // Line number on which the problematic_region begins, inclusive
    uint32_t lines_begin;
    // Line number on which the problematic_region ends, inclusive
    uint32_t lines_end;
};
std::ostream &operator<<(std::ostream &os, ParseError const &e);

// What kind of file the netlist is
enum class NetlistFileKind {
    // A generic spice file as defined by the ngspice user manual
    SPICE,
    // An XML eeschema file from KiCad
    EESCHEMA,
};

// We implement fault-tolerant parsers.
// netlist always contains a valid (but possibly empty) netlist
// If failures is empty, the parse was successful, and netlist contains correct data for the file
// If failures is nonempty, the parse failed, and it is generally not advised to access netlist
struct ParseResult {
    // The netlist generated from this parse
    std::unique_ptr<Netlist> netlist;
    // The failures that occurred during parse
    std::vector<ParseError> failures;
    // What kind of file was detected (or specified at callsite if not auto-detecting)
    NetlistFileKind detectedKind;

    inline bool is_ok() { return failures.size() > 0; }
};

// Parse the file assuming it is a generic "spice" file as defined by the ngspice user manual
// See https://ngspice.sourceforge.io/docs.html
// Version 45 was referenced while writing this function
// ParseResult.detectedKind will always equal SPICE
ParseResult parse_spice(std::string_view spice_file_contents, CompilerOptions opts = {});

// Parse the file assuming it is an eeschema KiCad schematic file
// ParseResult.detectedKind will always equal EESCHEMA
ParseResult parse_eeschema(std::string_view eeschema_file_contents, CompilerOptions opts = {});

// Attempt to discovert the file's type, then call appropriate parse_TYPE function.
// Only one kind of parse will be attempted
// This is the recommended way to parse
// ParseResult.detectedKind will equal the kind attempted
ParseResult auto_parse(std::string_view file_contents, std::string_view file_name,
                       CompilerOptions opts = {});

std::vector<ProgrammingCommand> compile(Netlist const &design, IrlTopology const &topology,
                                        CompilerOptions opts = {});

} // namespace irl
