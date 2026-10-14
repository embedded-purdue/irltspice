#include <cassert>
#include <cstdlib>
#include <iterator>
#include <vector>

#include <irl/compiler.hpp>

#include "irl/compiler/parse.hpp"

namespace irl {

ParseResult parse_spice(std::string_view spice_file_contents, std::string_view spice_file_name,
                        CompilerOptions opts) {
    SpiceParser parser(opts);
    return parser.try_parse(spice_file_name, spice_file_contents);
}

ParseResult parse_eeschema(std::string_view eeschema_file_contents,
                           std::string_view eeschema_file_name, CompilerOptions opts) {
    EeschemaParser parser(opts);
    return parser.try_parse(eeschema_file_name, eeschema_file_contents);
}

// Attempt to discovert the file's type, then call appropriate parse_TYPE function.
// Only one kind of parse will be attempted
// This is the recommended way to parse
// ParseResult.detectedKind will equal the kind attempted
ParseResult auto_parse(std::string_view file_contents, std::string_view file_name,
                       CompilerOptions opts) {

    auto parsers = AllParsersFactory::make_parsers_prioritized(file_name, opts);

    std::vector<ParseError> failures;

    for (const std::unique_ptr<INetlistParser> &parser : parsers) {
        if (opts.should_verbose_parse) {
            opts.log_fd << "Attempting parser: " << parser->parser_name() << "\n";
        }
        ParseResult r = parser->try_parse(file_name, file_contents);
        if (r.is_ok()) {
            return r;
        } else {
            failures.insert(failures.end(), std::make_move_iterator(r.failures.begin()),
                            std::make_move_iterator(r.failures.end()));
        }
    }

    return {
        .netlist{},
        .failures{std::move(failures)},
        .detectedKind = NetlistFileKind::SPICE,
    };
}

std::vector<ProgrammingCommand> compile(Netlist const &design, Netlist const &topology) {
    (void)design;
    (void)topology;
    // TODO: parsing, verification, and routing.
    assert(0 && "irl::compile is not implemented yet");
    std::abort();
}

} // namespace irl
