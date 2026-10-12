#include <memory>
#include <stdexcept>

#include "core/parse.hpp"
#include "irl/netlist.hpp"

using namespace std;
using namespace irl;

bool EeschemaParser::matches_filename(const string &filename) {
    return filename.ends_with(".xml");
};

unique_ptr<Netlist> EeschemaParser::try_parse(const std::string &filename, string_view in) const {
    throw runtime_error("Eeschema parsing not yet implemented.");
}
