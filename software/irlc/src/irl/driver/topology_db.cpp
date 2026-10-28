#include "irl/driver/topology_db.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>

#include "irl/detail/util.hpp"
#include "irl/driver/builtin_topos.hpp"
#include "irl/driver/topology_json.hpp"

namespace irl::driver {

namespace {

// Parses a raw template JSON into a cached, immutable BoardTemplate.
// The parsed kind must match the registration key; catching a mismatch here
// keeps the DB key and the JSON from silently disagreeing.
std::shared_ptr<const BoardTemplate>
make_template(nlohmann::json const &doc, BoardKindId expectedKind,
              std::optional<uint64_t> patchGuid = std::nullopt) {
    auto parsed = parseBoardTemplate(doc);
    IRL_ASSERT(parsed.kind == expectedKind,
               "Topology JSON kind does not match its registration key");
    parsed.patch_guid = patchGuid;
    return std::make_shared<const BoardTemplate>(std::move(parsed));
}

} // namespace

LazyTopologyDB::LazyTopologyDB(std::unordered_map<BoardKindId, nlohmann::json> knownKinds,
                               std::unordered_map<BoardGUID, std::string> knownPatches)
    : mKnownKinds(std::move(knownKinds)), mKnownPatches(std::move(knownPatches)) {}

void LazyTopologyDB::register_new_kind(BoardKindId kind, nlohmann::json json) {
    auto [it, inserted] = mKnownKinds.emplace(std::move(kind), std::move(json));
    IRL_ASSERT(inserted, "Kind collision: a topology is already registered for this board kind");

    // A newly registered kind overrides any builtin fallback that may already
    // have been cached, including board templates that inherited it.
    mKindTemplateCache.erase(it->first);
    for (auto boardIt = mBoardTemplateCache.begin(); boardIt != mBoardTemplateCache.end();) {
        if (boardIt->first.boardKind == it->first) {
            boardIt = mBoardTemplateCache.erase(boardIt);
        } else {
            ++boardIt;
        }
    }
}

void LazyTopologyDB::register_new_patch(BoardGUID guid, std::string patch) {
    uint64_t const uniqueId = guid.uniqueId;
    auto [it, inserted] = mKnownPatches.emplace(std::move(guid), std::move(patch));
    IRL_ASSERT(inserted, "Patch collision on unique id: {}", uniqueId);

    // Invalidate any board template that was cached before this patch existed.
    mBoardTemplateCache.erase(it->first);
}

void LazyTopologyDB::register_new_patch(BoardGUID guid, nlohmann::json const &patch) {
    IRL_ASSERT(patch.is_array(), "Topology patch must be an RFC 6902 array");
    register_new_patch(std::move(guid), patch.dump());
}

//======================
// Fetching from the DB
//======================

std::optional<std::reference_wrapper<const BoardTemplate>>
LazyTopologyDB::get_template_for_board(BoardGUID board) const {
    if (auto it = mBoardTemplateCache.find(board); it != mBoardTemplateCache.end()) {
        return std::cref(*it->second);
    }

    auto patchIt = mKnownPatches.find(board);
    if (patchIt == mKnownPatches.end()) {
        // No patch for this board: share the kind template.
        auto kindTmpl = get_or_make_template_for_kind(board.boardKind);
        if (!kindTmpl) {
            return std::nullopt;
        }
        auto [it, inserted] = mBoardTemplateCache.emplace(board, kindTmpl);
        return std::cref(*it->second);
    }

    auto base = get_json_for_kind(board.boardKind);
    if (!base.has_value()) {
        return std::nullopt;
    }

    // The registry layer already correlated this patch with this board, so
    // just apply it. applyPatch asserts the array shape.
    nlohmann::json doc = base->get();
    auto patch = parseJsonc(patchIt->second);
    applyPatch(doc, patch);

    auto tmpl = make_template(doc, board.boardKind, board.uniqueId);
    auto [it, inserted] = mBoardTemplateCache.emplace(board, std::move(tmpl));
    return std::cref(*it->second);
}

std::optional<std::reference_wrapper<const BoardTemplate>>
LazyTopologyDB::get_template_for_kind(BoardKindId kind) const {
    auto tmpl = get_or_make_template_for_kind(kind);
    if (!tmpl) {
        return std::nullopt;
    }
    return std::cref(*tmpl);
}

std::shared_ptr<const BoardTemplate>
LazyTopologyDB::get_or_make_template_for_kind(BoardKindId kind) const {
    if (auto it = mKindTemplateCache.find(kind); it != mKindTemplateCache.end()) {
        return it->second;
    }

    auto json = get_json_for_kind(kind);
    if (!json.has_value()) {
        return nullptr;
    }

    auto tmpl = make_template(json->get(), kind);
    auto [it, inserted] = mKindTemplateCache.emplace(kind, std::move(tmpl));
    return it->second;
}

std::optional<std::reference_wrapper<const nlohmann::json>>
LazyTopologyDB::get_json_for_kind(BoardKindId kind) const {
    if (auto it = mKnownKinds.find(kind); it != mKnownKinds.end()) {
        return std::cref(it->second);
    }

    auto builtin = get_builtin_topology_for_kind(kind);
    if (!builtin.has_value()) {
        return std::nullopt;
    }

    auto [it, inserted] = mKnownKinds.emplace(kind, parseJsonc(*builtin));
    return std::cref(it->second);
}

std::optional<std::string_view>
LazyTopologyDB::get_builtin_topology_for_kind(BoardKindId kind) const {
    if (kind.boardClass == BoardClass::MOTHER && kind.hardwareRevision == 0 &&
        kind.daughterKind == DaughterboardKind::UNKNOWN) {
        return std::string_view(mother_rev1);
    }
    // No builtin daughterboard topologies yet.
    return std::nullopt;
}

} // namespace irl::driver
