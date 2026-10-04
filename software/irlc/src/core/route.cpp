#include "core/board_info.hpp"
#include "core/compiler.hpp"
#include "core/netlist.hpp"
#include "core/numbers.hpp"
#include "core/verify.hpp"
#include "util/boost_util.hpp"
#include "util/macros.hpp"
#include <algorithm>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/properties.hpp>
#include <boost/range/adaptor/indexed.hpp>
#include <boost/range/iterator_range_core.hpp>
#include <cassert>
#include <core/route.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <ostream>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

using namespace std;
using namespace boost;

std::ostream &operator<<(std::ostream &os, CrossbarCon const &val) {
    os << "Connection {" << (int)val.crossbar_id << ", r:" << (int)val.row << ", c:" << (int)val.col
       << "}";
    return os;
}

void SimpleTspiceRouter::prune_unconnected_nets(RawNetlist &netlist) {
    pair verts = boost::vertices(netlist);
    auto current = verts.first;
    while (current != verts.second) {
        RawVert prev = *current;
        current++;

        if (netlist[prev].kind == NET && netlist[prev].value.net_value == WIRE &&
            in_degree(prev, netlist) < 2) {
            clear_vertex(prev, netlist);
            remove_vertex(prev, netlist);
        }
    }
}

void StdCell::to_str(std::ostream &ostream, RawNetlist const raw) {
    ostream << "Cell (" << id << "):";
    ostream << "\n  Output buffer: " << raw[buffer].name;
    ostream << "\n  Nets (" << nets.size() << "): ";
    for (auto &v : nets) {
        ostream << raw[v].name << ", ";
    }
    ostream << "\n  Components (" << components.size() << "): ";
    for (auto &v : components) {
        ostream << raw[v].name << ", ";
    }
    ostream << "\n  Output net: " << raw[pre_buffer_output_net].name;
    ostream << "\n  Input nets (" << input_nets.size() << "): ";
    for (auto &v : input_nets) {
        ostream << "(" << raw[v.first].name << " = ";
        visit(overloads{
                  [&](uint32_t cid) { ostream << "Cell " << cid; },
                  [&](NetKind special) { ostream << "Special net: " << net_name(special); },
              },
              v.second);
        ostream << "), ";
    }
    ostream << "\n";
}

StdCell const &AssignedNetlist::get_cell(RawVert v) {
    // NOTE:
    // Could just make this an unordered_map but I like saving memory :)
    for (auto &cell : cells) {
        if (cell.buffer == v) {
            return cell;
        }
    }
    throw runtime_error("AssignedNetlist::get_cell called with vert that is not a cell's buffer.");
}

/* clang-format off */
// Cells start white, get marked gray when in queue, then balck once finished
//
// See boost::breadth_first_visit in for implementation
//
// The concept/interface this needs to conform to
// https://www.boost.org/doc/libs/latest/libs/graph/doc/BFSVisitor.html
// Described here
// ==============
// Initialize Vertex 	vis.initialize_vertex(s, g) 	void 	This is invoked on every vertex of the graph before the start of the graph search.
// Discover Vertex 	vis.discover_vertex(u, g) 	void 	This is invoked when a vertex is encountered for the first time.
// Examine Vertex 	vis.examine_vertex(u, g) 	void 	This is invoked on a vertex as it is popped from the queue. This happens immediately before examine_edge() is invoked on each of the out-edges of vertex u.
// Examine Edge 	vis.examine_edge(e, g)  	void 	This is invoked on every out-edge of each vertex after it is discovered.
// Tree Edge    	vis.tree_edge(e, g)     	void 	This is invoked on each edge as it becomes a member of the edges that form the search tree.
// Non-Tree Edge 	vis.non_tree_edge(e, g) 	void 	This is invoked on back or cross edges for directed graphs and cross edges for undirected graphs.
// Gray Target  	vis.gray_target(e, g)   	void 	This is invoked on the subset of non-tree edges whose target vertex is colored gray at the time of examination. The color gray indicates that the vertex is currently in the queue. 
// Black Target 	vis.black_target(e, g)  	void 	This is invoked on the subset of non-tree edges whose target vertex is colored black at the time of examination. The color black indicates that the vertex has been removed from the queue.
// Finish Vertex 	vis.finish_vertex(u, g) 	void 	This invoked on a vertex after all of its out edges have been added to the search tree and all of the adjacent vertices have been discovered (but before the out-edges of the adjacent vertices have been examined).
/* clang-format on */
class CellAssigningVisitor : public boost::bfs_visitor<> {
  public:
    using colormap = std::unordered_map<RawVert, boost::default_color_type>;
    using idmap = std::unordered_map<RawVert, uint32_t>;
    using prevmap = std::unordered_map<RawVert, RawVert>;

    StdCell &cell;
    colormap &vertex_coloring;
    prevmap &previous_map;
    idmap const &buffer_outputs;
    IrlCompiler const &compiler;

    CellAssigningVisitor(StdCell &cell, auto &colormap, auto &buffer_outputs, auto &previous_map,
                         IrlCompiler const &compiler)
        : cell(cell), vertex_coloring(colormap), buffer_outputs(buffer_outputs),
          previous_map(previous_map), compiler(compiler) {}

    // This gets called right before the target's color is checked, so here
    // we can make all the other buffers black, and if we are going into a buffer's input we can
    // emit an error.
    void examine_edge(RawEdge e, const RawNetlist &g) {

        auto edge = g[e];
        auto u = boost::source(e, g);
        auto v = boost::target(e, g);
        if (compiler.opts.should_verbose_cell_assign())
            compiler.log_fd << "Edge from [" << g[u].name << "] to [" << g[v].name << "]\n";

        // If we are starting...
        //   if we're leaving out the input of the buffer we want to do normal stuff.
        //   But, we want to prevent leaving out the output.
        if (u == cell.buffer && g[e].kind == PIN_CELL_BUFFER_OUT) {
            assert(g[v].kind == NET);
            // We mark this as black, but if we have a circuit that loops back it should still get
            // visited later
            vertex_coloring[v] = boost::black_color;
            // At that point it will be recognized as a buffer output and added to the cell's inputs
            assert(buffer_outputs.contains(v));
        } else if (g[v].kind == NET) {
            // Here we are going to a net. Lots of special cases here.
            switch (g[v].value.net_value) {
            case V_HIGH:
            case V_NEG:
            case V_GND:
            case CIR_INPUT:
                if (compiler.opts.should_verbose_cell_assign())
                    compiler.log_fd << "  " << net_name(g[v].value.net_value) << "\n";
                if (buffer_outputs.contains(v)) {
                    rule_failed<"Buffer must not drive voltage">();
                }
                // We do want this special net as a net
                // it could be routable from top top-level or built right into the cell
                if (vertex_coloring[v] == boost::white_color) {
                    cell.nets.push_back(v);
                    cell.input_nets.push_back(pair{v, g[v].value.net_value});
                }
                // We don't want to visit it, though, because it leads to other cells
                vertex_coloring[v] = boost::black_color;
                break;
            case CIR_OUTPUT:
                // Since the output is driven by a cell, we treat it as just another cell output to
                // be routed back to us through the main switch. Thus we can fall through and re-use
                // the wire logic.
                if (!buffer_outputs.contains(v)) {
                    rule_failed<"Circuit output must be driven by only a buffer">();
                }
                // Intentionally fall through. NO BREAK
            case WIRE:
                if (vertex_coloring[v] == boost::white_color) {
                    // Add this guy to our nets!
                    cell.nets.push_back(v);
                    // If it's the output of another cell we don't want to visit it cuz it could
                    // feed into other cells We also need to mark it as an input to this cell.
                    auto other_out = buffer_outputs.find(v);
                    if (other_out != buffer_outputs.end()) {
                        vertex_coloring[v] = boost::black_color;
                        uint32_t other_id = (*other_out).second;
                        cell.input_nets.push_back(pair{v, other_id});
                    }
                }
                break;
            }
            return;
        } else {
            // We are coming from a net to a component
            // We should only bfs into wires. If this failed then the net branch in this big if else
            // statement is messed up and failed to mark it as black.
            assert(g[u].value.net_value == WIRE);
            switch (g[v].kind) {
            case R:
            case C:
            case DIODE:
            case CUSTOM:
                // Simple passive. We just add and go.
                if (vertex_coloring[v] == boost::white_color)
                    cell.components.push_back(v);
                break;
            case OPAMP:
                // If we are coming in through v+ or v- we fucked up.
                if (g[e].kind == PIN_OPAMP_SUPPLY_PLUS) {
                    rule_failed<"Opamp supply plus must be V_HIGH or unconnectd">();
                }
                if (g[e].kind == PIN_OPAMP_SUPPLY_MINUS) {
                    rule_failed<"Opamp supply minus must be V_NEG or unconnectd">();
                }
                // Otherwise just add and go!
                if (vertex_coloring[v] == boost::white_color)
                    cell.components.push_back(v);
                break;
            case CELL_BUFFER:
                if (vertex_coloring[v] == boost::white_color) {
                    // This is terrible. We should not get here.
                    // Always a terrible terrible bad day.
                    if (g[e].kind == PIN_CELL_BUFFER_OUT) {
                        if (!buffer_outputs.contains(u)) {
                            throw runtime_error(string("Somehow, ") + g[u].name +
                                                " is not in the buffer outputs set, but it targets "
                                                "a cell buffer with an out edge.");
                        } else {
                            throw runtime_error(string("Even though") + g[u].name +
                                                " is a cell output it still got visited.");
                        }
                    } else {
                        assert(g[e].kind == PIN_CELL_BUFFER_IN);
                        // This just means they messed up their circuit.
                        // Emit a normal error
                        ostringstream bad_chain(
                            "Illegal connection between the inputs of two cell output buffers: ");
                        bad_chain << g[v].name;
                        auto current = u;
                        while (previous_map.contains(u)) {
                            bad_chain << ", " << g[u].name;
                            u = previous_map[u];
                        }
                        bad_chain << ", " << g[cell.buffer].name;
                        throw runtime_error(bad_chain.str());
                    }
                }
                break;
            case Q_PNP:
            case Q_NPN:
                throw runtime_error("TODO");
                break;
            case NET:
                // Unreachable. Compiler should be able to get this one I hope
                break;
            }
        }
    }

    // Here to record the chain of how we got to this node so we can print a nice message :)
    void tree_edge(RawEdge e, const RawNetlist &g) { previous_map[target(e, g)] = source(e, g); }
};

unique_ptr<AssignedNetlist> SimpleTspiceRouter::try_assign(unique_ptr<RawNetlist> &raw) {

    unique_ptr<AssignedNetlist> assigned(new AssignedNetlist{});
    assigned->cells = vector<StdCell>{};

    auto cell_buffs_iter = pair_to_iter(boost::vertices(*raw)) |
                           views::filter([&](auto x) { return (*raw)[x].kind == CELL_BUFFER; });

    // Collect so we can iter twice.
    //  Once to pre-populate cell->buffer fields, once to do rest of fields.
    //  1st needed cuz giving input_nets a cell_id needs to know which buffer is which cell
    vector<RawVert> cell_buffs(cell_buffs_iter.begin(), cell_buffs_iter.end());
    std::unordered_map<RawVert, uint32_t> buffer_outputs{};

    // Enumerate does not exist in C++20 i'm gonna kill myself this language is actually such a
    // shitshow. In 20 they had the beautiful rust iterators to base this off of and they couldn't
    // even implement collect, enumerate, or zip. So so dogshit. I should not have to write a for
    // loop that's incrementing like this in 2026. It's not even rust it's just any language with a
    // half-decent iterator system. Lowkey should use Boost but the documentation is so rough in BGL
    // so I'm too wary.
    uint32_t i = 0;
    for (RawVert buff : cell_buffs) {
        assigned->cells.push_back(StdCell{
            .id = i,
            .buffer = buff,
        });
        for (RawEdge e : pair_to_iter(out_edges(buff, *raw))) {
            if ((*raw)[e].kind == PIN_CELL_BUFFER_OUT) {
                buffer_outputs[target(e, *raw)] = i;
                if ((*raw)[target(e, *raw)].value.net_value == CIR_OUTPUT) {
                    if (assigned->output_cell != UINT32_MAX) {
                        rule_failed<"Circuit output must be driven by only a buffer">();
                    }
                    assigned->output_cell = i;
                    if (compiler.opts.should_verbose_cell_assign()) {
                        compiler.log_fd << "Cell id: " << i << ", is the output cell\n";
                    }
                }
            } else {
                assert((*raw)[e].kind == PIN_CELL_BUFFER_IN);
                assigned->cells[i].pre_buffer_output_net = target(e, *raw);
            }
        }
        i++;
    }

    CellAssigningVisitor::prevmap pmap{};
    CellAssigningVisitor::colormap cmap{};

    for (StdCell &cell : assigned->cells) {

        if (compiler.opts.should_verbose_cell_assign())
            compiler.log_fd << "Assigning cell: " << cell.id
                            << ", which starts at: " << (*raw)[cell.buffer].name << "\n";

        cell.nets = vector<RawVert>{};
        cell.components = vector<RawVert>{};

        pmap.clear();
        cmap.clear();
        CellAssigningVisitor vis = CellAssigningVisitor(cell, cmap, buffer_outputs, pmap, compiler);

        boost::breadth_first_search(
            *raw, cell.buffer, boost::visitor(vis).color_map(boost::make_assoc_property_map(cmap)));

        if (compiler.opts.should_verbose_cell_assign())
            cell.to_str(compiler.log_fd, *raw);
    }

    // Only after success can we take ownership of raw
    assigned->raw_list = std::move(raw);
    raw = nullptr;

    return assigned;
}

// Attempts to make a connection from a toplvl row satisfying toplvl_predicate to a cell row in
// phy/design_cell
bool SimpleTspiceRouter::make_routing_connection(
    std::vector<CrossbarCon> &connections, netmap_t &netmap, free_cells_t &free_cells,
    PhysStdCell const &phy_cell, StdCell const &design_cell, RawVert net_id,
    std::function<bool(boost::range::index_value<const RowCon &>)> toplvl_predicate,
    std::function<std::string(void)> err_msg_gen) {
    auto cell_id = design_cell.id;
    auto phy_cell_crossbar_id = phy_cell.crossbars.bars[0].id;
    // Step one is make sure we have a routable row in the cell that's free
    auto cell_row = phy_cell.crossbars.rows();
    auto cell_row_enumed =
        boost::make_iterator_range(cell_row.begin(), cell_row.end()) | boost::adaptors::indexed(0);
    auto cell_routable_row =
        std::find_if(cell_row_enumed.begin(), cell_row_enumed.end(), [&](auto c) {
            // Assume all routable conns are the same
            auto row_i = c.index();
            return std::holds_alternative<RoutableRowCon>(c.value()) && free_cells[cell_id][row_i];
        });
    if (cell_routable_row == cell_row_enumed.end()) {
        throw RoutingError(cell_id,
                           "Cell has too many special nets for its number of routable rows");
    }

    // Now we allocate the row in the cell
    assert(free_cells[cell_id][cell_routable_row->index()]);
    netmap[net_id] = cell_routable_row->index();
    free_cells[cell_id][cell_routable_row->index()] = false;
    // Now we find the special cell in the top level
    auto toplvl_row = board.root.rows();
    auto toplvl_row_enumed = boost::make_iterator_range(toplvl_row.begin(), toplvl_row.end()) |
                             boost::adaptors::indexed(0);
    auto toplvl_net_row =
        std::find_if(toplvl_row_enumed.begin(), toplvl_row_enumed.end(), toplvl_predicate);
    if (toplvl_net_row == toplvl_row_enumed.end()) {
        throw RoutingError(cell_id, err_msg_gen());
        // throw RoutingError(cell_id, (ostringstream() << "Special net :" << net_name(net_kind) <<
        // " not routable from top level or within in cell").str());
    }

    auto toplvl_routable_col =
        std::find_if(board.root.cols_begin(), board.root.cols_end(), [&](auto c) {
            RoutableColCon const *t = std::get_if<RoutableColCon>(&c);
            return t && t->child_id == phy_cell_crossbar_id &&
                   t->child_row == cell_routable_row->index();
        });
    if (toplvl_routable_col == board.root.cols_end()) {
        throw RoutingError(
            cell_id, (ostringstream()
                      << "Top level crossbar does not have connection to child crossbar: { id: "
                      << (int)phy_cell_crossbar_id << ", row: " << cell_routable_row->index()
                      << "}")
                         .str());
    }

    // Make the actual connection let's fucking gooooo
    connections.push_back(CrossbarCon{
        .crossbar_id = toplvl_routable_col.get_bar_phys_id(),
        .row = (uint8_t)toplvl_net_row->index(),
        .col = toplvl_routable_col.get_bar_col(),
    });
    if (compiler.opts.should_verbose_connections()) {
        compiler.log_fd << "Connection {" << (int)toplvl_routable_col.get_bar_phys_id()
                        << ", r:" << (int)toplvl_net_row->index()
                        << ", c:" << (int)toplvl_routable_col.get_bar_col() << "} made\n";
    }

    return true;
}

ProgrammingInfo SimpleTspiceRouter::do_routing(unique_ptr<AssignedNetlist> &assigned) {
    vector<CrossbarCon> connections{};
    vector<ResistorMapping> resistances{};
    // Step 1: Net allocation. which rows are used for which nets
    // We first make a map of which root rows are free
    // And one for which std cell rows are free
    free_cells_t free_cells{};
    // free_cells.resize(board.cells.size());
    for (auto const &cell : board.cells) {
        free_cells.push_back(vector(cell.crossbars.rows().size(), true));
    }

    // First, a non-optional connection is the output cell's output to the circuit output.
    {
        // Find the output column
        ColConIter output_col =
            std::find_if(board.root.cols_begin(), board.root.cols_end(), [](const ColCon &con) {
                if (SpecialNetCon const *spnet = std::get_if<SpecialNetCon>(&con)) {
                    if (spnet->kind == CIR_OUTPUT) {
                        return true;
                    }
                }
                return false;
            });
        if (output_col == board.root.cols_end()) {
            throw RoutingError(-1, "Top level crossbar has no circuit output column");
        }
        // Find the top-level row that corresponds to the output cell's output
        assert(assigned->output_cell != UINT32_MAX);
        auto input_row = std::find_if(
            board.root.rows().begin(), board.root.rows().end(), [&assigned](RowCon const &con) {
                return std::visit(overloads{[&assigned](BufferOutputRowCon b) {
                                                return b.id == assigned->output_cell;
                                            },
                                            [](auto a) { return false; }},
                                  con);
            });

        connections.push_back(CrossbarCon{
            .crossbar_id = output_col.get_bar_phys_id(),
            .row = static_cast<uint8_t>(input_row - board.root.rows().begin()),
            .col = output_col.get_bar_col(),
        });
        if (compiler.opts.should_verbose_connections()) {
            // Dumbass language needs a cast or gets formatted as ascii
            compiler.log_fd << "Output connection is Bar: " << (size_t)output_col.get_bar_phys_id()
                            << ", Row: " << input_row - board.root.rows().begin()
                            << ", Col: " << (size_t)output_col.get_bar_col() << "\n";
        }
    }

    // Now we need to make the map of where nets are actually physically located
    // This map is a temporary for each cell.
    // We populate it with nets, then go thru the components and connect them to their nets
    netmap_t netmap{};
    std::set<decltype(ComponentColCon{}.id)> taken_components{};
    size_t i = -1;
    for (auto const &cell : assigned->cells) {
        netmap.clear();
        taken_components.clear();
        i++;
        auto const &phy_cell = board.cells[i];
        auto phy_cell_crossbar_id = phy_cell.crossbars.bars[0].id;
        // Priority 1 is the output. Locate it on the standard cell
        for (RowCon const &row : phy_cell.crossbars.rows()) {
            int row_i = &row - &phy_cell.crossbars.rows()[0];
            if (std::holds_alternative<BufferInputRowCon>(row)) {
                // No for-else in c++ hence goto. Also no enumerate hence the pointer arithmetic.
                // dogshit language.
                if (compiler.opts.should_verbose_connections())
                    compiler.log_fd << "Found output!\n";
                assert(free_cells[i][row_i]);
                netmap[cell.pre_buffer_output_net] = row_i;
                free_cells[i][row_i] = false;
                goto found_output;
            }
        }
        throw RoutingError(i, "No output row on crossbar");
    found_output:
        if (compiler.opts.should_verbose_connections())
            compiler.log_fd << "Looking for inputs\n";
        // Priority 2 is cell inputs.
        for (pair<RawVert, CellInputId> const &input : cell.input_nets) {
            bool we_good = std::visit(
                overloads{
                    [&](NetKind net_kind) {
                        // We need to find out how to get the special net here.
                        // 2 options:
                        //  1. It's already hard-wired to a cell row
                        //  2. It's routable form the top level.
                        // Let's start by chekcing option 1:
                        for (RowCon const &row : phy_cell.crossbars.rows()) {
                            int row_i = &row - &phy_cell.crossbars.rows()[0];
                            SpecialNetCon const *con = std::get_if<SpecialNetCon>(&row);
                            if (con && con->kind == net_kind) {
                                // Should always be free, as there's only one of each special net in
                                // a cell (cuz like there's only one ground in the whole circuit
                                // e.g.)
                                assert(free_cells[i][row_i]);
                                netmap[input.first] = row_i;
                                free_cells[i][row_i] = false;
                                if (compiler.opts.should_verbose_connections()) {
                                    compiler.log_fd << "Special net : " << net_name(net_kind)
                                                    << " allocated to row: " << row_i << "\n";
                                }
                                return true;
                            }
                        }

                        if (compiler.opts.should_verbose_connections()) {
                            compiler.log_fd << "Making connection for " << net_name(net_kind)
                                            << "\n";
                        }

                        // Uh oh. We need to route this from the top level.
                        return this->make_routing_connection(
                            connections, netmap, free_cells, phy_cell, cell, input.first,
                            [net_kind](auto c) {
                                SpecialNetCon const *t = std::get_if<SpecialNetCon>(&c.value());
                                return t && t->kind == net_kind;
                            },
                            [net_kind]() {
                                return (ostringstream()
                                        << "Special net: " << net_name(net_kind)
                                        << " not routable from top level or within in cell")
                                    .str();
                            });
                    },
                    [&](uint32_t input_cell) {
                        if (compiler.opts.should_verbose_connections()) {
                            compiler.log_fd << "Making connection from cell " << input_cell
                                            << " to cell " << cell.id << "\n";
                        }

                        // Need to route from top lvl
                        return this->make_routing_connection(
                            connections, netmap, free_cells, phy_cell, cell, input.first,
                            [input_cell](auto c) {
                                BufferOutputRowCon const *t =
                                    std::get_if<BufferOutputRowCon>(&c.value());
                                return t && t->id == input_cell;
                            },
                            [input_cell]() {
                                return (ostringstream()
                                        << "Output from cell: " << input_cell
                                        << " not routable from top level or within in cell")
                                    .str();
                            });
                    },
                },
                input.second);

            if (!we_good)
                throw RoutingError(i, "No input row compatible with input: " +
                                          (*assigned->raw_list)[input.first].name);
        }

        // Priority 3 is cell normal nets
        for (RawVert net : cell.nets) {
            // If we've already mapped the net, continue
            if (netmap.contains(net)) {
                continue;
            }
            // Else find first available row
            for (int row_i = 0; row_i < phy_cell.crossbars.rows().size(); row_i++) {
                auto &con = phy_cell.crossbars.rows()[0];
                if (free_cells[i][row_i] && (std::holds_alternative<FloatingCon>(con) ||
                                             std::holds_alternative<RoutableRowCon>(con))) {
                    free_cells[i][row_i] = false;
                    netmap[net] = row_i;
                    goto found_net;
                }
            }
            throw RoutingError(i, "User has requested too many nets for cell");
        found_net:
            continue;
        }

        if (compiler.opts.should_verbose_connections()) {
            compiler.log_fd << "All nets allocated for cell " << i << '\n';
            for (std::pair<RawVert, size_t> m : netmap) {
                compiler.log_fd << "  " << (*assigned->raw_list)[m.first].name << "=" << m.second
                                << "\n";
            }
        }

        // Now we have allocated every net and we should route the cell's components
        for (auto const &component_vert : cell.components) {
            RawNetlistVertexInfo const &component = (*assigned->raw_list)[component_vert];

            // Find a valid physcial component
            ColConIter phy_component_pin_1 =
                std::find_if(phy_cell.crossbars.cols_begin(), phy_cell.crossbars.cols_end(),
                             [&component, &taken_components](ColCon const &col) {
                                 ComponentColCon const *comp_con =
                                     std::get_if<ComponentColCon>(&col);
                                 if (comp_con && comp_con->kind == component.kind) {
                                     bool good = true;
                                     if (comp_con->kind == C) {
                                         good = comp_con->val == component.value.numeric_value;
                                     }
                                     return good && !taken_components.contains(comp_con->id);
                                 }
                                 return false;
                             });
            if (phy_component_pin_1 == phy_cell.crossbars.cols_end()) {
                throw RoutingError(
                    i, string("Cannot find free physical component matching design component: ") +
                           component.name);
            }
            // Find all its pins
            auto phy_component_id = std::get<ComponentColCon>(*phy_component_pin_1).id;
            auto phy_component_all_pins =
                std::ranges::subrange(phy_component_pin_1, phy_cell.crossbars.cols_end()) |
                views::filter([&phy_component_id](ColCon const &col) {
                    ComponentColCon const *comp_con = std::get_if<ComponentColCon>(&col);
                    return comp_con && comp_con->id == phy_component_id;
                }) |
                views::transform([](ColCon const &col) { return std::get<ComponentColCon>(col); });

            taken_components.insert(phy_component_id);

            if (component.kind == R) {
                resistances.push_back(ResistorMapping{
                    .resistor_id = (uint8_t)phy_component_id,
                    .desired_val = component.value.numeric_value,
                });
                if (compiler.opts.should_verbose_connections()) {
                    compiler.log_fd << "Design resistor: " << component.name
                                    << ", mapped to physical id: " << phy_component_id << "\n";
                }
            }

            // Now take the design edges, find their corresponding physical edges, and make those
            // connections! Yeah!
            auto design_edges = out_edges(component_vert, *assigned->raw_list);
            auto design_edge = design_edges.first;
            while (design_edge != design_edges.second) {
                for (auto pin = phy_component_all_pins.begin(); pin != phy_component_all_pins.end();
                     pin++) {
                    RawEdge netlist_edge = *design_edge;
                    RawNetlistEdgeInfo const &edge = (*assigned->raw_list)[netlist_edge];
                    RawVert net_vert = target(netlist_edge, *assigned->raw_list);
                    RawNetlistVertexInfo const &net = (*assigned->raw_list)[net_vert];
                    ComponentColCon const &pin_info = *pin;
                    if (pin_info.pin_kind != edge.kind) {
                        continue;
                    }
                    // we have found the pin to assign. Begin assembling the connection!
                    // This supplies col and chip
                    ColConIter col_info = pin.base().base();

                    uint8_t bar_id = col_info.get_bar_phys_id();
                    uint8_t col = col_info.get_bar_col();
                    // Now we just get row
                    // Can't go straight to it bc of swizzle in chaining
                    assert(netmap.contains(net_vert));
                    size_t base_row_id = netmap[net_vert];

                    size_t phys_row_id = phy_cell.crossbars.chained_row_ind(bar_id, base_row_id);

                    CrossbarCon con{
                        .crossbar_id = bar_id,
                        .row = (uint8_t)phys_row_id,
                        .col = col,
                    };
                    if (compiler.opts.should_verbose_connections()) {
                        compiler.log_fd << "Connection from component: " << component.name
                                        << " to net: " << net.name << ", is: " << con << "\n";
                    }
                    connections.push_back(con);
                    design_edge++;
                    if (design_edge == design_edges.second) {
                        break;
                    }
                }
            }
        }
    }

    return {
        .connections = connections,
        .resistances = quantize_resistors(resistances),
    };
}

constexpr size_t max_quant = (1 << RESOLUTION_BITS) - 1;
constexpr val_pico_t step_size = MAX_R.v / (1 << RESOLUTION_BITS);

constexpr val_pico_t un_quant(size_t const &quant) { return val_pico_t((quant + 1) * step_size.v); }
constexpr size_t quant(val_pico_t const &r) {
    if (r < val_pico_t(step_size.v / 2)) {
        return 0;
    }
    size_t q = ((r - val_pico_t(step_size.v / 2)) / step_size).v;
    if (q >= max_quant) {
        return max_quant;
    } else {
        return q;
    }
}

std::vector<QuantizedResistance>
SimpleTspiceRouter::quantize_resistors(std::vector<ResistorMapping> const &resistances) {
    auto quantized_iter =
        resistances | views::transform([](auto const &r) {
            return QuantizedResistance{.resistor_id = r.resistor_id,
                                       .value = static_cast<uint8_t>(quant(r.desired_val))};
        });
    std::vector<QuantizedResistance> quantized(quantized_iter.begin(), quantized_iter.end());
    if (compiler.opts.should_verbose_connections()) {
        for (auto r : quantized | adaptors::indexed(0)) {
            auto const &i = r.index();
            auto desired = resistances[i].desired_val;
            auto actual = un_quant(r.value().value);
            double percent_error = (abs((double)actual.v - desired.v) / desired.v) * 100;
            compiler.log_fd << "Resistor: " << desired << " -> " << actual << " (" << percent_error
                            << "% error)\n";
        }
    }
    return quantized;
}
