// Tests for the lazy topology database: kind/board factories, builtin fallback,
// lazy parsing, caching, registration semantics, and patch application.

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "irl/command.hpp"
#include "irl/driver/topology_db.hpp"

using namespace irl;
using namespace irl::driver;

namespace {

BoardKindId mother_rev0() { return BoardKindId::motherboard(0); }

BoardGUID board_for(BoardKindId kind, uint64_t uniqueId) { return BoardGUID::from(kind, uniqueId); }

nlohmann::json minimal_mother_json(uint32_t revision) {
    return nlohmann::json::parse(R"({
        "class": "MOTHER",
        "revision": )" + std::to_string(revision) +
                                 R"(,
        "topology": { "components": {}, "ports": {} }
    })");
}

} // namespace

TEST_CASE("BoardKindId factories derive from command.hpp board types", "[topology][db]") {
    MotherboardTopology mother{};
    mother.uniqueId = 42;
    mother.hardwareRevision = 3;

    BoardKindId kind = BoardKindId::from(mother);
    REQUIRE(kind.boardClass == BoardClass::MOTHER);
    REQUIRE(kind.hardwareRevision == 3);
    REQUIRE(kind.daughterKind == DaughterboardKind::UNKNOWN);
    REQUIRE(kind == BoardKindId::motherboard(3));

    BoardGUID guid = BoardGUID::from(mother);
    REQUIRE(guid.uniqueId == 42);
    REQUIRE(guid.boardKind == kind);

    DaughterboardTopology daughter{};
    daughter.uniqueId = 7;
    daughter.kind = DaughterboardKind::BASIC_V1;

    BoardKindId daughterKind = BoardKindId::from(daughter);
    REQUIRE(daughterKind.boardClass == BoardClass::DAUGHTER);
    REQUIRE(daughterKind.hardwareRevision == 0);
    REQUIRE(daughterKind.daughterKind == DaughterboardKind::BASIC_V1);
    REQUIRE(daughterKind == BoardKindId::daughterboard(DaughterboardKind::BASIC_V1));

    BoardGUID daughterGuid = BoardGUID::from(daughter);
    REQUIRE(daughterGuid.uniqueId == 7);
    REQUIRE(daughterGuid.boardKind == daughterKind);
}

TEST_CASE("Board IDs can be used as unordered_map keys", "[topology][db]") {
    std::unordered_map<BoardKindId, int> byKind;
    byKind[BoardKindId::motherboard(0)] = 1;
    byKind[BoardKindId::daughterboard(DaughterboardKind::BASIC_V1)] = 2;
    REQUIRE(byKind.at(BoardKindId::motherboard(0)) == 1);
    REQUIRE(byKind.at(BoardKindId::daughterboard(DaughterboardKind::BASIC_V1)) == 2);
    REQUIRE(byKind.size() == 2);

    std::unordered_map<BoardGUID, int> byGuid;
    byGuid[board_for(mother_rev0(), 10)] = 1;
    byGuid[board_for(mother_rev0(), 11)] = 2;
    REQUIRE(byGuid.at(board_for(mother_rev0(), 10)) == 1);
    REQUIRE(byGuid.at(board_for(mother_rev0(), 11)) == 2);
    REQUIRE(byGuid.size() == 2);
}

TEST_CASE("Builtin topologies are available by kind and cached", "[topology][db]") {
    LazyTopologyDB db;

    auto first = db.get_template_for_kind(mother_rev0());
    REQUIRE(first.has_value());
    BoardTemplate const &tmpl = first->get();
    REQUIRE(tmpl.kind == mother_rev0());
    REQUIRE_FALSE(tmpl.patch_guid.has_value());
    REQUIRE(tmpl.netlist.get_vertex("U1").has_value());

    // A second lookup returns the same cached object.
    auto second = db.get_template_for_kind(mother_rev0());
    REQUIRE(second.has_value());
    REQUIRE(&first->get() == &second->get());
}

TEST_CASE("Unknown kinds have no template", "[topology][db]") {
    LazyTopologyDB db;
    REQUIRE_FALSE(db.get_template_for_kind(BoardKindId::motherboard(99)).has_value());
    REQUIRE_FALSE(db.get_template_for_kind(BoardKindId::daughterboard(DaughterboardKind::BASIC_V1))
                      .has_value());
}

TEST_CASE("register_new_kind stores custom kinds incrementally", "[topology][db]") {
    LazyTopologyDB db;
    BoardKindId kind1 = BoardKindId::motherboard(1);
    BoardKindId kind2 = BoardKindId::motherboard(2);

    db.register_new_kind(kind1, minimal_mother_json(1));
    db.register_new_kind(kind2, minimal_mother_json(2));

    auto tmpl1 = db.get_template_for_kind(kind1);
    REQUIRE(tmpl1.has_value());
    REQUIRE(tmpl1->get().kind.hardwareRevision == 1);

    auto tmpl2 = db.get_template_for_kind(kind2);
    REQUIRE(tmpl2.has_value());
    REQUIRE(tmpl2->get().kind.hardwareRevision == 2);
}

TEST_CASE("Patches apply per board and are cached", "[topology][db]") {
    LazyTopologyDB db;
    BoardGUID board = board_for(mother_rev0(), 1234);

    auto patch = nlohmann::json::parse(R"([
        { "op": "remove", "path": "/topology/components/U1" }
    ])");
    db.register_new_patch(board, patch);

    auto tmpl = db.get_template_for_board(board);
    REQUIRE(tmpl.has_value());
    REQUIRE(tmpl->get().patch_guid == 1234);
    REQUIRE_FALSE(tmpl->get().netlist.get_vertex("U1").has_value());

    // Cached.
    auto tmpl2 = db.get_template_for_board(board);
    REQUIRE(tmpl2.has_value());
    REQUIRE(&tmpl->get() == &tmpl2->get());
}

TEST_CASE("Patch string overload stores raw text", "[topology][db]") {
    LazyTopologyDB db;
    BoardGUID board = board_for(mother_rev0(), 2345);

    std::string patch = R"([
        { "op": "remove", "path": "/topology/components/U1" }
    ])";
    db.register_new_patch(board, patch);

    auto tmpl = db.get_template_for_board(board);
    REQUIRE(tmpl.has_value());
    REQUIRE(tmpl->get().patch_guid == 2345);
    REQUIRE_FALSE(tmpl->get().netlist.get_vertex("U1").has_value());
}

TEST_CASE("A no-op patch is still applied and marked", "[topology][db]") {
    LazyTopologyDB db;
    BoardGUID board = board_for(mother_rev0(), 3456);

    // Replacing the revision with its existing value changes nothing, but the
    // registry supplied a patch for this board, so it is still marked patched.
    auto patch = nlohmann::json::parse(R"([
        { "op": "replace", "path": "/revision", "value": 0 }
    ])");
    db.register_new_patch(board, patch);

    auto tmpl = db.get_template_for_board(board);
    REQUIRE(tmpl.has_value());
    REQUIRE(tmpl->get().patch_guid == 3456);
    REQUIRE(tmpl->get().netlist.get_vertex("U1").has_value());
}

TEST_CASE("register_new_patch stores patches incrementally", "[topology][db]") {
    LazyTopologyDB db;
    BoardGUID board1 = board_for(mother_rev0(), 4567);
    BoardGUID board2 = board_for(mother_rev0(), 4568);

    auto removing = nlohmann::json::parse(R"([
        { "op": "remove", "path": "/topology/components/U1" }
    ])");
    auto adding = nlohmann::json::parse(R"([
        { "op": "add", "path": "/topology/components/R1",
          "value": { "kind": "R", "numeric": 1000, "pins": [] } }
    ])");

    db.register_new_patch(board1, removing);
    db.register_new_patch(board2, adding);

    auto tmpl1 = db.get_template_for_board(board1);
    REQUIRE(tmpl1.has_value());
    REQUIRE(tmpl1->get().patch_guid == 4567);
    REQUIRE_FALSE(tmpl1->get().netlist.get_vertex("U1").has_value());

    auto tmpl2 = db.get_template_for_board(board2);
    REQUIRE(tmpl2.has_value());
    REQUIRE(tmpl2->get().patch_guid == 4568);
    REQUIRE(tmpl2->get().netlist.get_vertex("U1").has_value());
    REQUIRE(tmpl2->get().netlist.get_vertex("R1").has_value());
}

TEST_CASE("Constructing with known kinds and patches works", "[topology][db]") {
    std::unordered_map<BoardKindId, nlohmann::json> kinds;
    BoardKindId kind = BoardKindId::motherboard(7);
    kinds.emplace(kind, minimal_mother_json(7));

    std::unordered_map<BoardGUID, std::string> patches;
    BoardGUID board = board_for(kind, 42);
    patches.emplace(board, nlohmann::json::parse(R"([
                        { "op": "add", "path": "/topology/components/R1",
                          "value": { "kind": "R", "numeric": 1000, "pins": [] } }
                    ])")
                               .dump());

    LazyTopologyDB db(std::move(kinds), std::move(patches));

    auto kindTmpl = db.get_template_for_kind(kind);
    REQUIRE(kindTmpl.has_value());
    REQUIRE(kindTmpl->get().kind.hardwareRevision == 7);

    auto boardTmpl = db.get_template_for_board(board);
    REQUIRE(boardTmpl.has_value());
    REQUIRE(boardTmpl->get().patch_guid == 42);
    // The patch leaves the kind untouched and adds a component.
    REQUIRE(boardTmpl->get().kind.hardwareRevision == 7);
    REQUIRE(boardTmpl->get().netlist.get_vertex("R1").has_value());
}
