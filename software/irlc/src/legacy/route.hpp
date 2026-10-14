#pragma once

#include <boost/range/adaptor/indexed.hpp>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <variant>
#include <vector>

#include "core/board_info.hpp"
#include "core/compiler.hpp"
#include "core/netlist.hpp"
#include "core/numbers.hpp"

typedef std::variant<NetKind, uint32_t> CellInputId;

typedef struct StdCell {
    // This cell's arbitrary id. Must be the same as index in AssignedNetList->cells vec
    uint32_t id;
    // This cell's output buffer
    RawVert buffer;
    // Nets in this cell including inputs and output
    std::vector<RawVert> nets;
    // Components in this cell. Not counting output buffer
    std::vector<RawVert> components;
    // The net right before the output buffer. Element of nets field
    RawVert pre_buffer_output_net;
    // input nets and which std cell the come from. Subset of nets field
    std::vector<std::pair<RawVert, CellInputId>> input_nets;

    void to_str(std::ostream &ostream, RawNetlist const raw);
} StdCell;

typedef struct CrossbarCon {
    uint32_t crossbar_id;
    uint8_t row;
    uint8_t col;
} CrossbarCon;

typedef struct ResistorMapping {
    uint8_t resistor_id;
    val_pico_t const &desired_val;
} ResistorMapping;

typedef struct QuantizedResistance {
    uint8_t resistor_id;
    uint8_t value;
} QuantizedResistance;

typedef struct ProgrammingInfo {
    std::vector<CrossbarCon> connections;
    std::vector<QuantizedResistance> resistances;
} ProgrammingInfo;

std::ostream &operator<<(std::ostream &os, CrossbarCon const &val);

constexpr val_pico_t MAX_R = 200_k;
constexpr size_t RESOLUTION_BITS = 7;

// An AssignedNetList has assigned each component (not each net) to std cells
// The raw_list should not be modified, as this struct may hold references to it.
typedef struct _AssignedNetList {
    std::unique_ptr<const RawNetlist> raw_list;
    std::vector<StdCell> cells;
    // The cell which drives the output
    uint32_t output_cell = UINT32_MAX;

    // Maps a vert for a cell's buffer to a cell reference.
    StdCell const &get_cell(RawVert v);
} AssignedNetlist;

class RoutingError : public std::runtime_error {
  public:
    RoutingError(int32_t cell_id, const std::string &what)
        : runtime_error(
              (std::ostringstream() << "<Error routing cell: " << cell_id << "> " << what).str()) {}
};

typedef std::unordered_map<RawVert, size_t> netmap_t;
typedef std::vector<std::vector<bool>> free_cells_t;

// This guy makes a couple big assumptions that highly couple it to our specific board design:
//   1. The only constant voltage cells have access to is ground.
//   2. Cells follow a tree-like structure where all can be routed to through a single root crossbar
class SimpleTspiceRouter {
  private:
    IrlCompiler const &compiler;
    SimpleTspiceInfo const &board;

    bool make_routing_connection(
        std::vector<CrossbarCon> &connections, netmap_t &netmap, free_cells_t &free_cells,
        PhysStdCell const &phy_cell, StdCell const &design_cell, RawVert net_id,
        std::function<bool(boost::range::index_value<const RowCon &>)> toplvl_predicate,
        std::function<std::string(void)> err_msg_gen);

  public:
    SimpleTspiceRouter(IrlCompiler const &compiler, auto const &board)
        : compiler(compiler), board(board) {};

    // Remove nets that are unconnected.
    void prune_unconnected_nets(RawNetlist &netlist);

    // Either returns null, and raw remains a valid pointer, or returns a real pointer and raw is
    // invalidated (moved to the returned AssignedNetlist).
    std::unique_ptr<AssignedNetlist> try_assign(std::unique_ptr<RawNetlist> &raw);

    ProgrammingInfo do_routing(std::unique_ptr<AssignedNetlist> &assigned);

    std::vector<QuantizedResistance>
    quantize_resistors(std::vector<ResistorMapping> const &resistances);
};
