#include "core/board_info.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <variant>

#include "core/numbers.hpp"
#include "util/macros.hpp"

using namespace std;

size_t ChainedCrossbar::base_row_ind(RowCon const &row) const {
    ChainedRowCon const *chained_ptr = get_if<ChainedRowCon>(&row);
    if (chained_ptr) {
        // Follow the chain
        return chained_ptr->sibling;
    } else {
        // Not chained must be in first row or we should freak out
        // Thou shalt not fear pointer arithmetic
        RowCon const *addr = &row;
        RowCon const *base = &bars[0].rows[0];
        if (addr < base || addr >= base + bars[0].rows.size()) {
            throw runtime_error("Passed a RowCon not in this crossbar.");
        } else {
            return addr - base;
        }
    }
}

size_t ChainedCrossbar::chained_row_ind(uint8_t phys_xbar_id, size_t base_row_ind) const {
    auto const &bar =
        std::find_if(bars.begin(), bars.end(), [&](auto const &x) { return x.id == phys_xbar_id; });
    if (bar == bars.end()) {
        throw runtime_error("Bad xbar id in chained_row_ind");
    }

    size_t i = 0;
    for (auto const &row : bar->rows) {
        auto r = this->base_row_ind(row);
        if (r == base_row_ind) {
            return i;
        }
        i++;
    }
    throw runtime_error(
        (ostringstream() << "No row in physical bar mapping to base row id: " << base_row_ind)
            .str());
};

ColConIter ChainedCrossbar::cols_begin() const { return ColConIter(this->bars); }

ColConIter::ColConIter(std::vector<PhysCrossbar> const &bars) {
    this->bars = &bars;
    this->bar_id = 0;
    this->col_id = 0;
}

ColConIter::ColConIter() { this->bars = nullptr; }

ColConIter::reference ColConIter::operator*() const { return (*bars)[bar_id].cols[col_id]; }

ColCon const *ColConIter::operator->() const { return &(*bars)[bar_id].cols[col_id]; }

// Prefix
ColConIter &ColConIter::operator++() {

    // Don't incr the too-far
    if (bars != nullptr) {
        col_id++;
        // Use a while in case we get a fucked crossbar with zero pins or something
        while (col_id >= (*bars)[bar_id].cols.size()) {
            bar_id++;
            if (bar_id >= bars->size()) {
                bars = nullptr;
                return *this;
            }
            col_id = 0;
        }
    }

    return *this;
}

// Postfix
ColConIter ColConIter::operator++(int) {
    ColConIter temp = *this;

    ++*this;

    return temp;
}

bool ColConIter::operator==(ColConIter const &other) const {
    if (bars == other.bars) {
        if (bars == nullptr) {
            return true;
        } else {
            return bar_id == other.bar_id && col_id == other.col_id;
        }
    } else {
        return false;
    }
}
bool ColConIter::operator!=(ColConIter const &other) const { return !(*this == other); }

uint8_t ColConIter::get_bar_phys_id() { return (*bars)[bar_id].id; }
uint8_t ColConIter::get_bar_col() { return col_id; }

// Simple shit to double check and make sure I'm not insane
// Should prolly not really be used for production
bool validate_simple_tspice(SimpleTspiceInfo const &board) {
    unordered_map<uint8_t, PhysCrossbar const *> mappy;
    unordered_map<int32_t, val_pico_t> val_mappy;
    for (auto &bar : board.root.bars) {
        mappy[bar.id] = &bar;
    }
    for (auto &cell : board.cells) {
        for (auto &bar : cell.crossbars.bars) {
            mappy[bar.id] = &bar;
        }
    }

    for (auto &id_barptr_pair : mappy) {
        for (auto &rowcon : id_barptr_pair.second->rows) {
            // FUCKASS POINTER ARITHMETIC BECAUSE WE DON'T HAVE GODDAMN ENUMERATE
            auto rowid = &rowcon - &id_barptr_pair.second->rows[0];
            bool isgood = visit(
                overloads{[&](RoutableRowCon rt) {
                              ColCon parent = mappy[rt.parent_id]->cols[rt.parent_col];
                              if (!holds_alternative<RoutableColCon>(parent)) {
                                  return false;
                              }
                              RoutableColCon rc = get<RoutableColCon>(parent);
                              return rc.child_id == id_barptr_pair.first && (rc.child_row == rowid);
                          },
                          [](auto &other) { return true; }},
                rowcon);
            if (!isgood) {
                cout << "Bad row: " << (uint32_t)id_barptr_pair.first << ", " << rowid << "\n";
                return false;
            } else {
                continue;
                cout << "Good row: " << (uint32_t)id_barptr_pair.first << ", " << rowid << "\n";
            }
        }
        for (auto &colcon : id_barptr_pair.second->cols) {
            auto colid = &colcon - &id_barptr_pair.second->cols[0];
            bool isgood = visit(overloads{[&](RoutableColCon rt) {
                                              RowCon child = mappy[rt.child_id]->rows[rt.child_row];
                                              if (!holds_alternative<RoutableRowCon>(child)) {
                                                  return false;
                                              }
                                              RoutableRowCon rc = get<RoutableRowCon>(child);
                                              return rc.parent_id == id_barptr_pair.first &&
                                                     (rc.parent_col == colid);
                                          },
                                          [&](ComponentColCon cc) {
                                              if (cc.kind == C) {
                                                  if (!board.valid_caps.contains(cc.val)) {
                                                      return false;
                                                  }
                                              }
                                              if (val_mappy.contains(cc.id)) {
                                                  if (val_mappy[cc.id] != cc.val) {
                                                      return false;
                                                  } else {
                                                      return true;
                                                  }
                                              } else {
                                                  val_mappy[cc.id] = cc.val;
                                                  return true;
                                              }
                                          },
                                          [](auto &other) { return true; }},
                                colcon);
            if (!isgood) {
                cout << "Bad col: " << (uint32_t)id_barptr_pair.first << ", " << colid << "\n";
                return false;
            } else {
                continue;
                cout << "Good col: " << (uint32_t)id_barptr_pair.first << ", " << colid << "\n";
            }
        }
    }
    return true;
}
