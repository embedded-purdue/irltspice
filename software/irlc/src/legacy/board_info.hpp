#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "core/netlist.hpp"
#include "core/numbers.hpp"
#include "irl/netlist.hpp"
#include "util/macros.hpp"

using namespace irl;
using enum irl::NetKind;

typedef std::variant<struct FloatingCon, struct SpecialNetCon, struct RoutableColCon,
                     struct ComponentColCon>
    ColCon;
typedef std::variant<struct FloatingCon, struct SpecialNetCon, struct RoutableRowCon,
                     struct ChainedRowCon, struct BufferOutputRowCon, struct BufferInputRowCon>
    RowCon;

// CONNECTION KINDS

// A row/col that is literally just connected to nothing. Good for routing within a cell
typedef struct FloatingCon {
} FloatingCon;

// A row/col that is hard-wired to either a voltage source or circuit input/output
// The kind is not allowed to be WIRE
typedef struct SpecialNetCon {
    NetKind kind;
} SpecialNetCon;

// A simple connection from a crossbar to a component
typedef struct ComponentColCon {
    // The kind of component this column goes to
    NetlistVertexKind kind;
    // What pin kind of the component this column goes to
    NetlistEdgeKind pin_kind;
    // Id of the component this column goes to.
    // For programmable components (resistors) this must be what the micro recognizes, for
    // everything else this just must be unique.
    int32_t id;
    // Value of the component if it is a fixed value component (e.g. a cap)
    val_pico_t val = val_pico_t(0);
} ComponentColCon;

// A connection from this child crossbar to a parent crossbar, allowing routing
typedef struct RoutableRowCon {
    uint8_t parent_id;
    // which col of the parent this row is connected to
    uint8_t parent_col;
} RoutableRowCon;

// A connection from this parent crossbar to child parent crossbar, allowing routing
typedef struct RoutableColCon {
    uint8_t child_id;
    // which row of the child this col is connected to
    uint8_t child_row;
} RoutableColCon;

// A row that is hard wired to a buffer input
typedef struct BufferInputRowCon {
} BufferInputRowCon;

// A row that is hard wired to a std cell's buffer output
typedef struct BufferOutputRowCon {
    uint32_t id;
} BufferOutputRowCon;

// A row that is chained to the row of another crossbar
typedef struct ChainedRowCon {
    // The id of the row in the root crossbar of a ChainedCrossbar
    uint8_t sibling;
} ChainedRowCon;

// A physical crossbar on the board and what it is connected to.
// By convention "rows" are used to connect "cols" together.
// So in a std cell rows are the nets and cols are the components
typedef class PhysCrossbar {
  public:
    // This crossbar's id as recognized by the micro
    uint8_t id;
    std::vector<RowCon> rows;
    std::vector<ColCon> cols;
} PhysCrossbar;

// Iterator over the columns of a ChainedCrossbar
class ColConIter {

    // Ptr for nullability
    std::vector<PhysCrossbar> const *bars;
    size_t bar_id;
    size_t col_id;

  public:
    using value_type = const ColCon;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using reference = ColCon const &;
    using pointer = ColCon const *;

    reference operator*() const;
    pointer operator->() const;
    ColConIter &operator++();
    ColConIter operator++(int);
    bool operator==(ColConIter const &other) const;
    bool operator!=(ColConIter const &other) const;

    // Gets the bars physical id
    uint8_t get_bar_phys_id();
    uint8_t get_bar_col();

    // Returns the "too far" aka end iterator
    ColConIter();
    ColConIter(const ColConIter &) = default;
    ColConIter &operator=(const ColConIter &) = default;
    // Makes a normal iterator pointing to the first elem
    ColConIter(std::vector<PhysCrossbar> const &bars);
};

// One or more crossbars whose rows are connected together.
// Improperly setting up the chaining leads to undefined behavior
typedef class ChainedCrossbar {
  public:
    // Trivial chain (no swizzle)
    // template <typename... PCross> inline ChainedCrossbar(PCross... nbars);
    inline ChainedCrossbar(std::initializer_list<PhysCrossbar> nbars);
    template <size_t N_BARS, size_t N_ROWS>
    inline ChainedCrossbar(
        std::array<PhysCrossbar, N_BARS> bars,
        std::array<std::array<std::pair<size_t, size_t>, N_ROWS>, N_BARS - 1> swizzles);

    std::vector<PhysCrossbar> bars;

    inline std::span<const RowCon> rows() const { return bars[0].rows; }

    size_t base_row_ind(RowCon const &row) const;
    // Maps an index of the base row to its row on a (potentially non-base) crossbar
    size_t chained_row_ind(uint8_t phys_xbar_id, size_t base_row_ind) const;

    ColConIter cols_begin() const;
    inline ColConIter cols_end() const { return ColConIter(); };
} ChainedCrossbar;

// A physical standard cell, with bar and buffer
// Should be onle one crossbar row that has a BufferRowCon.
typedef struct PhysStdCell {
    // Id of this cell. Should match id of buffer in the cell as referenced by the row con
    uint8_t id;
    ChainedCrossbar crossbars;
} PhysStdCell;

// Every std cell is the same. All are routable to each other via a single parent chained crossbar
typedef struct SimpleTspiceInfo {
    ChainedCrossbar root;
    std::vector<PhysStdCell> cells;
    std::set<val_pico_t> valid_caps;
} SimpleTspiceInfo;

// Checks that all the routable connections line up.
bool validate_simple_tspice(SimpleTspiceInfo const &board);

// Board defs
#define ROOT_BAR 0
#define CELL_0_BAR_0 1
#define CELL_0_BAR_1 2
#define CELL_1_BAR_0 3
#define CELL_1_BAR_1 4
#define CELL_2_BAR_0 5
#define CELL_2_BAR_1 6
#define CELL_3_BAR_0 7
#define CELL_3_BAR_1 8

const SimpleTspiceInfo MAIN_BOARD = []() {
    uint8_t i = 0;
    auto uniquie = [&i]() { return --i; };
    auto prev = [&i]() { return i; };

    // One of the macros of all time
#define STD_CELL(CELL_ID, FIRST_R, C1, C2, C3)                                                     \
    PhysStdCell {                                                                                  \
        .id = CELL_ID,                                                                             \
        .crossbars = ChainedCrossbar(                                                              \
            /* clang-format off */ /* It hates this template idk */            \
                std::array<PhysCrossbar,2>{ /* clang-format on */                                                   \
                           PhysCrossbar{                                                           \
                               .id = CELL_##CELL_ID##_BAR_0,                                       \
                               .rows =                                                             \
                                   std::vector<RowCon>{                                            \
                                       RoutableRowCon{.parent_id = ROOT_BAR, .parent_col = C1},    \
                                       RoutableRowCon{.parent_id = ROOT_BAR, .parent_col = C2},    \
                                       RoutableRowCon{.parent_id = ROOT_BAR, .parent_col = C3},    \
                                       BufferInputRowCon{},                                        \
                                       SpecialNetCon{.kind = V_GND},                               \
                                       FloatingCon{},                                              \
                                       FloatingCon{},                                              \
                                       FloatingCon{},                                              \
                                   },                                                              \
                               .cols =                                                             \
                                   std::vector<ColCon>{                                            \
                                       ComponentColCon{                                            \
                                           .kind = R, .pin_kind = PIN_R, .id = FIRST_R + 5},       \
                                       ComponentColCon{                                            \
                                           .kind = R, .pin_kind = PIN_R, .id = FIRST_R + 5},       \
                                       ComponentColCon{                                            \
                                           .kind = R, .pin_kind = PIN_R, .id = FIRST_R + 6},       \
                                       ComponentColCon{                                            \
                                           .kind = R, .pin_kind = PIN_R, .id = FIRST_R + 6},       \
                                       ComponentColCon{                                            \
                                           .kind = CUSTOM,                                         \
                                           .pin_kind =                                             \
                                               PIN_CUSTOM,                                         \
                                           .id = uniquie()},                                       \
                                       ComponentColCon{                                            \
                                           .kind = CUSTOM, .pin_kind = PIN_CUSTOM, .id = prev()},  \
                                       ComponentColCon{.kind = DIODE,                              \
                                                       .pin_kind = PIN_DIODE_K,                    \
                                                       .id = uniquie()},                           \
                                       ComponentColCon{                                            \
                                           .kind = DIODE, .pin_kind = PIN_DIODE_A, .id = prev()},  \
                                       ComponentColCon{.kind = C,                                  \
                                                       .pin_kind = PIN_C,                          \
                                                       .id = uniquie(),                            \
                                                       .val = 1_n},                                \
                                       ComponentColCon{.kind = C,                                  \
                                                       .pin_kind = PIN_C,                          \
                                                       .id = prev(),                               \
                                                       .val = 1_n},                                \
                                       ComponentColCon{.kind = C,                                  \
                                                       .pin_kind = PIN_C,                          \
                                                       .id = uniquie(),                            \
                                                       .val = 10_n},                               \
                                       ComponentColCon{.kind = C,                                  \
                                                       .pin_kind = PIN_C,                          \
                                                       .id = prev(),                               \
                                                       .val = 10_n},                               \
                                       ComponentColCon{.kind = DIODE,                              \
                                                       .pin_kind = PIN_DIODE_K,                    \
                                                       .id = uniquie()},                           \
                                       ComponentColCon{                                            \
                                           .kind = DIODE, .pin_kind = PIN_DIODE_A, .id = prev()},  \
                                       ComponentColCon{.kind = C,                                  \
                                                       .pin_kind = PIN_C,                          \
                                                       .id = uniquie(),                            \
                                                       .val = 100_n},                              \
                                       ComponentColCon{.kind = C,                                  \
                                                       .pin_kind = PIN_C,                          \
                                                       .id = prev(),                               \
                                                       .val = 100_n},                              \
                                   },                                                              \
                           },                                                                      \
                           PhysCrossbar{                                                           \
                               .id = CELL_##CELL_ID##_BAR_1,                                       \
                               .rows = std::vector<RowCon>(8),                                     \
                               .cols =                                                             \
                                   std::vector<ColCon>{                                            \
                                       ComponentColCon{.kind = OPAMP,                              \
                                                       .pin_kind = PIN_OPAMP_OUT,                  \
                                                       .id = uniquie()},                           \
                                       ComponentColCon{.kind = OPAMP,                              \
                                                       .pin_kind = PIN_OPAMP_PLUS,                 \
                                                       .id = prev()},                              \
                                       ComponentColCon{.kind = OPAMP,                              \
                                                       .pin_kind = PIN_OPAMP_MINUS,                \
                                                       .id = prev()},                              \
                                       ComponentColCon{.kind = OPAMP,                              \
                                                       .pin_kind = PIN_OPAMP_OUT,                  \
                                                       .id = uniquie()},                           \
                                       ComponentColCon{.kind = OPAMP,                              \
                                                       .pin_kind = PIN_OPAMP_MINUS,                \
                                                       .id = prev()},                              \
                                       ComponentColCon{.kind = OPAMP,                              \
                                                       .pin_kind = PIN_OPAMP_PLUS,                 \
                                                       .id = prev()},                              \
                                       ComponentColCon{                                            \
                                           .kind = R, .pin_kind = PIN_R, .id = FIRST_R + 1},       \
                                       ComponentColCon{                                            \
                                           .kind = R, .pin_kind = PIN_R, .id = FIRST_R + 1},       \
                                       ComponentColCon{                                            \
                                           .kind = R, .pin_kind = PIN_R, .id = FIRST_R + 2},       \
                                       ComponentColCon{                                            \
                                           .kind = R, .pin_kind = PIN_R, .id = FIRST_R + 2},       \
                                       ComponentColCon{                                            \
                                           .kind = R, .pin_kind = PIN_R, .id = FIRST_R + 4},       \
                                       ComponentColCon{                                            \
                                           .kind = R, .pin_kind = PIN_R, .id = FIRST_R + 4},       \
                                                                                                   \
                                       ComponentColCon{.kind = C,                                  \
                                                       .pin_kind = PIN_C,                          \
                                                       .id = uniquie(),                            \
                                                       .val = 1_u},                                \
                                       ComponentColCon{.kind = C,                                  \
                                                       .pin_kind = PIN_C,                          \
                                                       .id = prev(),                               \
                                                       .val = 1_u},                                \
                                       ComponentColCon{                                            \
                                           .kind = R, .pin_kind = PIN_R, .id = FIRST_R + 3},       \
                                       ComponentColCon{                                            \
                                           .kind = R, .pin_kind = PIN_R, .id = FIRST_R + 3},       \
                                   },                                                              \
                           }},                                                                     \
            std::array<std::array<std::pair<size_t, size_t>, 8>, 1>{                               \
                std::array{std::pair(0_z, 4_z), std::pair(1_z, 5_z), std::pair(2_z, 6_z),          \
                           std::pair(3_z, 7_z), std::pair(4_z, 0_z), std::pair(5_z, 1_z),          \
                           std::pair(6_z, 2_z), std::pair(7_z, 3_z)}})                             \
    }

    return SimpleTspiceInfo{
        .root = ChainedCrossbar({PhysCrossbar{
            .id = ROOT_BAR,
            .rows =
                std::vector<RowCon>{
                    SpecialNetCon{.kind = V_HIGH},
                    FloatingCon{},
                    SpecialNetCon{.kind = V_NEG},
                    BufferOutputRowCon{.id = 3},
                    SpecialNetCon{.kind = CIR_INPUT},
                    BufferOutputRowCon{.id = 0},
                    BufferOutputRowCon{.id = 1},
                    BufferOutputRowCon{.id = 2},
                },
            .cols =
                std::vector<ColCon>{
                    RoutableColCon{.child_id = CELL_0_BAR_0, .child_row = 2},
                    RoutableColCon{.child_id = CELL_0_BAR_0, .child_row = 1},
                    RoutableColCon{.child_id = CELL_0_BAR_0, .child_row = 0},
                    RoutableColCon{.child_id = CELL_1_BAR_0, .child_row = 2},
                    RoutableColCon{.child_id = CELL_1_BAR_0, .child_row = 1},
                    RoutableColCon{.child_id = CELL_1_BAR_0, .child_row = 0},
                    RoutableColCon{.child_id = CELL_2_BAR_0, .child_row = 2},
                    RoutableColCon{.child_id = CELL_3_BAR_0, .child_row = 0},
                    RoutableColCon{.child_id = CELL_3_BAR_0, .child_row = 1},
                    RoutableColCon{.child_id = CELL_3_BAR_0, .child_row = 2},
                    SpecialNetCon{.kind = CIR_OUTPUT},
                    FloatingCon{},
                    FloatingCon{},
                    FloatingCon{},
                    RoutableColCon{.child_id = CELL_2_BAR_0, .child_row = 0},
                    RoutableColCon{.child_id = CELL_2_BAR_0, .child_row = 1},
                },
        }}),
        .cells = std::vector<PhysStdCell>{STD_CELL(0, -1, 2, 1, 0), STD_CELL(1, 5, 5, 4, 3),
                                          STD_CELL(2, 11, 14, 15, 6), STD_CELL(3, 17, 7, 8, 9)},
        .valid_caps = std::set{1_n, 10_n, 100_n, 1_u}};
}();

const SimpleTspiceInfo MINIMAL_MAIN = []() {
    uint8_t i = 0;
    auto uniquie = [&i]() { return --i; };
    auto prev = [&i]() { return i; };

    auto out =
        SimpleTspiceInfo{
            .root =
                ChainedCrossbar(
                    {PhysCrossbar{
                        .id = ROOT_BAR,
                        .rows =
                            std::vector<RowCon>{
                                SpecialNetCon{.kind = V_HIGH},
                                FloatingCon{},
                                SpecialNetCon{.kind = V_NEG},
                                FloatingCon{},
                                // BufferOutputRowCon{.id = 3},
                                SpecialNetCon{.kind = CIR_INPUT},
                                FloatingCon{},
                                // BufferOutputRowCon{.id = 0},
                                BufferOutputRowCon{.id = 0},
                                BufferOutputRowCon{.id = 1},
                            },
                        .cols =
                            std::vector<ColCon>{
                                FloatingCon{},
                                FloatingCon{},
                                FloatingCon{},
                                // RoutableColCon{.child_id = CELL_0_BAR_0, .child_row = 2},
                                // RoutableColCon{.child_id = CELL_0_BAR_0, .child_row = 1},
                                // RoutableColCon{.child_id = CELL_0_BAR_0, .child_row = 0},
                                // FloatingCon{},
                                // FloatingCon{},
                                // FloatingCon{},
                                RoutableColCon{.child_id = CELL_1_BAR_0, .child_row = 2},
                                RoutableColCon{.child_id = CELL_1_BAR_0, .child_row = 1},
                                RoutableColCon{.child_id = CELL_1_BAR_0, .child_row = 0},
                                RoutableColCon{.child_id = CELL_2_BAR_0, .child_row = 2},
                                FloatingCon{},
                                FloatingCon{},
                                FloatingCon{},
                                // RoutableColCon{.child_id = CELL_3_BAR_0, .child_row = 0},
                                // RoutableColCon{.child_id = CELL_3_BAR_0, .child_row = 1},
                                // RoutableColCon{.child_id = CELL_3_BAR_0, .child_row = 2},
                                SpecialNetCon{.kind = CIR_OUTPUT},
                                FloatingCon{},
                                FloatingCon{},
                                FloatingCon{},
                                RoutableColCon{.child_id = CELL_2_BAR_0, .child_row = 0},
                                RoutableColCon{.child_id = CELL_2_BAR_0, .child_row = 1},
                            },
                    }}),
            .cells = std::vector<PhysStdCell>{STD_CELL(1, 5, 5, 4, 3), STD_CELL(2, 11, 14, 15, 6)},
            .valid_caps = std::set{1_n, 10_n, 100_n, 1_u}};

    // "Binning"
    out.cells[1].crossbars.bars[1].cols[10] = FloatingCon{};
    out.cells[1].crossbars.bars[1].cols[11] = FloatingCon{};
    // Hack
    out.cells[0].id = 0;
    out.cells[1].id = 1;
    return out;
}();

const SimpleTspiceInfo MINIMAL_MAIN_CELL2_ONLY = []() {
    uint8_t i = 0;
    auto uniquie = [&i]() { return --i; };
    auto prev = [&i]() { return i; };

    auto out =
        SimpleTspiceInfo{.root = ChainedCrossbar({PhysCrossbar{
                             .id = ROOT_BAR,
                             .rows =
                                 std::vector<RowCon>{
                                     SpecialNetCon{.kind = V_HIGH},
                                     FloatingCon{},
                                     SpecialNetCon{.kind = V_NEG},
                                     FloatingCon{},
                                     // BufferOutputRowCon{.id = 3},
                                     SpecialNetCon{.kind = CIR_INPUT},
                                     FloatingCon{},
                                     // BufferOutputRowCon{.id = 0},
                                     FloatingCon{},
                                     // BufferOutputRowCon{.id = 0},
                                     BufferOutputRowCon{.id = 0},
                                 },
                             .cols =
                                 std::vector<ColCon>{
                                     FloatingCon{},
                                     FloatingCon{},
                                     FloatingCon{},
                                     // RoutableColCon{.child_id = CELL_0_BAR_0, .child_row = 2},
                                     // RoutableColCon{.child_id = CELL_0_BAR_0, .child_row = 1},
                                     // RoutableColCon{.child_id = CELL_0_BAR_0, .child_row = 0},
                                     FloatingCon{},
                                     FloatingCon{},
                                     FloatingCon{},
                                     // RoutableColCon{.child_id = CELL_1_BAR_0, .child_row = 2},
                                     // RoutableColCon{.child_id = CELL_1_BAR_0, .child_row = 1},
                                     // RoutableColCon{.child_id = CELL_1_BAR_0, .child_row = 0},
                                     RoutableColCon{.child_id = CELL_2_BAR_0, .child_row = 2},
                                     FloatingCon{},
                                     FloatingCon{},
                                     FloatingCon{},
                                     // RoutableColCon{.child_id = CELL_3_BAR_0, .child_row = 0},
                                     // RoutableColCon{.child_id = CELL_3_BAR_0, .child_row = 1},
                                     // RoutableColCon{.child_id = CELL_3_BAR_0, .child_row = 2},
                                     SpecialNetCon{.kind = CIR_OUTPUT},
                                     FloatingCon{},
                                     FloatingCon{},
                                     FloatingCon{},
                                     RoutableColCon{.child_id = CELL_2_BAR_0, .child_row = 0},
                                     RoutableColCon{.child_id = CELL_2_BAR_0, .child_row = 1},
                                 },
                         }}),
                         .cells = std::vector<PhysStdCell>{STD_CELL(2, 11, 14, 15, 6)},
                         .valid_caps = std::set{1_n, 10_n, 100_n, 1_u}};

    // "Binning"
    out.cells[0].crossbars.bars[1].cols[10] = FloatingCon{};
    out.cells[0].crossbars.bars[1].cols[11] = FloatingCon{};
    // Hack
    out.cells[0].id = 0;
    return out;
}();

static inline const SimpleTspiceInfo *get_board_info_by_name(std::string_view name) {
    if (name == "MAIN_BOARD") {
        return &MAIN_BOARD;
    } else if (name == "MINIMAL_MAIN") {
        return &MINIMAL_MAIN;
    } else if (name == "MINIMAL_MAIN_CELL2_ONLY") {
        return &MINIMAL_MAIN_CELL2_ONLY;
    } else {
        throw std::runtime_error((std::ostringstream() << "No board with name: " << name).str());
    }
}

template <size_t N_BARS, size_t N_ROWS>
inline ChainedCrossbar::ChainedCrossbar(
    std::array<PhysCrossbar, N_BARS> bars,
    std::array<std::array<std::pair<size_t, size_t>, N_ROWS>, N_BARS - 1> swizzles) {
    this->bars = std::vector(bars.begin(), bars.end());
    for (int i = 0; i < N_BARS - 1; i++) {
        this->bars[i + 1].rows.resize(N_ROWS);
        for (auto const &swizz : swizzles[i]) {
            this->bars[i + 1].rows[swizz.second] = ChainedRowCon{.sibling = (uint8_t)swizz.first};
        }
    }
}

inline ChainedCrossbar::ChainedCrossbar(std::initializer_list<PhysCrossbar> nbars) {
    for (PhysCrossbar const &bar : nbars) {
        this->bars.push_back(bar);
    }
    for (auto &bar : bars | std::views::drop(1)) {
        size_t n = bar.rows.size();
        bar.rows.clear();
        for (uint8_t i = 0; i < n; i++) {
            bar.rows.push_back(ChainedRowCon{.sibling = i});
        }
    }
}
