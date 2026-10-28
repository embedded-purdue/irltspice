#pragma once

#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <unordered_map>
#include <vector>

#include <irl/command.hpp>
#include <irl/netlist.hpp>

namespace irl::driver {

enum class BoardClass : uint8_t { MOTHER, DAUGHTER };

// All edges on netlist are ready-to-use
// All vertices on netlist are ready unless they are a PORT
//   - In that case, they are guaranteed to be pointed to by an entry in ports

struct BoardKindId {
    // Used for both
    BoardClass boardClass;
    // Only used for motherboards. Must be 0 for daughterboards
    uint32_t hardwareRevision = 0;
    // Only used for daughterboards. Must be UNKNOWN for motherboards
    DaughterboardKind daughterKind = DaughterboardKind::UNKNOWN;
};

struct BoardTemplate {
    BoardKindId kind;
    // If this was patched, which uniqueId was the source of the patch
    std::optional<uint64_t> patch_guid;
    Netlist netlist{};
    std::vector<NetlistVertexIdx> ports{};
};

struct BoardGUID {
    BoardKindId boardKind;
    uint64_t uniqueId;
};

// A database of all known topologies (patched for boards)
// Does not support updating topologies after insertion.
class LazyTopologyDB {
    // All kinds we know how to produce topologies for and their raw json (needed for patching)
    // If it's not here we don't know how to produce a topology
    std::unordered_map<BoardKindId, const nlohmann::json> mKnownKinds;
    // A cache which is a further parsed version of mKnownKinds. Un-patched boards hit this.
    std::unordered_map<BoardKindId, const std::shared_ptr<const BoardTemplate>> mKindTemplateCache;

    // all known patches by GUID. If it's not here it's not getting applied
    std::unordered_map<BoardGUID, const char *> mKnownPatches;
    // A cache of parsed boards by GUID.
    std::unordered_map<BoardGUID, const std::shared_ptr<const BoardTemplate>> mBoardTemplateCache;

    // Second. Checks mKnownKinds, otherwise falls back to get_builtin_topology_for_kind.
    // Caches if fallback hits
    std::optional<nlohmann::json const *> get_json_for_kind(BoardKindId kind);
    // Third.
    // Checks if we have a topology defined for a kind in builtin_topos.hpp
    std::optional<const char *> get_builtin_topology_for_kind(BoardKindId kind);

  public:
    // TODO:
    // Add a way to construct with some known kinds and patches
    LazyTopologyDB();

    // Only register new, does not update old
    void register_new_kind(BoardKindId kind, nlohmann::json &&json);
    // Only register new, does not update old
    void register_new_patch(BoardGUID guid, nlohmann::json &&patch);

    // First. Checks mKindTemplateCache, otherwise falls back to get_json_for_kind
    // Caches if fallback hits
    std::optional<std::shared_ptr<const BoardTemplate *>> get_netlist_for_kind(BoardKindId kind);

    // Checks mBoardTemplateCache, otherwise checks for a patch.
    // If patch exists (and is not a no-op), applies it to get_json_for_kind, parses and stores in
    // cache If no patch, calls get_netlist_for_kind and caches that result
    std::optional<std::shared_ptr<const BoardTemplate *>> get_netlist_for_board(BoardGUID board);
};

} // namespace irl::driver
