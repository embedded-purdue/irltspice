#include <string_view>

#include "irl/compiler.hpp"
#include "irl/compiler/parse.hpp"

using namespace std;
using namespace irl;

bool EeschemaParser::matches_filename(string_view file_name) {
    return file_name.ends_with(".xml");
};

ParseResult EeschemaParser::try_parse(string_view file_contets, string_view file_name) const {
    return ParseResult{
        .netlist{nullptr},
        .failures{ParseError{.message = "Eeschema parsing not yet implemented",
                             .problematic_region{file_contets},
                             .lines_begin = 0,
                             .lines_end = 0}},
        .detectedKind = NetlistFileKind::EESCHEMA,
    };
}
