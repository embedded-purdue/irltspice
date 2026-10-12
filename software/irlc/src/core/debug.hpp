#pragma once

#include <ostream>

#include "irl_netlist.hpp"

void debug_print_netlist(std::ostream &out, irl::Netlist const &netlist);
