#pragma once

// A database of known board topologies, keyed by board kind and (optionally)
// by per-board serial number.
//
// A "kind" identifies a board's class (mother/daughter), its hardware revision
// (mothers), or its daughterboard kind. A "GUID" additionally pins a specific
// physical board by serial number so that per-board patches can be applied.
//
// The database is lazy: raw JSON is kept as text and only parsed into a
// BoardTemplate when a caller asks for one. Results are cached. The database is
// single-threaded; caches are mutable so the getters can be const.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <irl/command.hpp>
#include <irl/netlist.hpp>

#include "util/macros.hpp"

namespace irl::driver {

enum class BoardClass : uint8_t { MOTHER, DAUGHTER };

// Identifies a board kind. Used as the key for unpatched templates.
//
// Invariants:
//   - MOTHER   => daughterKind == DaughterboardKind::UNKNOWN
//   - DAUGHTER => hardwareRevision == 0
struct BoardKindId {
    // Used for both.
    BoardClass boardClass = BoardClass::MOTHER;
    // Only used for motherboards. Must be 0 for daughterboards.
    uint32_t hardwareRevision = 0;
    // Only used for daughterboards. Must be UNKNOWN for motherboards.
    DaughterboardKind daughterKind = DaughterboardKind::UNKNOWN;

    constexpr bool operator==(BoardKindId const &) const = default;

    // Factories for the two board classes.
    static constexpr BoardKindId motherboard(uint32_t hardwareRevision) {
        return {.boardClass = BoardClass::MOTHER,
                .hardwareRevision = hardwareRevision,
                .daughterKind = DaughterboardKind::UNKNOWN};
    }
    static constexpr BoardKindId daughterboard(DaughterboardKind daughterKind) {
        return {.boardClass = BoardClass::DAUGHTER,
                .hardwareRevision = 0,
                .daughterKind = daughterKind};
    }

    // Factories from the protobuf-independent topology types in command.hpp.
    static BoardKindId from(MotherboardTopology const &mother) {
        return motherboard(mother.hardwareRevision);
    }
    static BoardKindId from(DaughterboardTopology const &daughter) {
        return daughterboard(daughter.kind);
    }
};

// A parsed board template.
//
// All edges on netlist are ready-to-use.
// All vertices on netlist are ready unless they are a PORT.
//   - In that case, they are guaranteed to be pointed to by an entry in ports.
struct BoardTemplate {
    BoardKindId kind;
    // If this was patched, which uniqueId was the source of the patch.
    std::optional<uint64_t> patch_guid;
    Netlist netlist{};
    std::vector<NetlistVertexIdx> ports{};
};

// A specific physical board: its kind plus its serial number.
struct BoardGUID {
    BoardKindId boardKind;
    uint64_t uniqueId;

    constexpr bool operator==(BoardGUID const &) const = default;

    // Factories.
    static constexpr BoardGUID from(BoardKindId kind, uint64_t uniqueId) {
        return {.boardKind = kind, .uniqueId = uniqueId};
    }
    static BoardGUID from(MotherboardTopology const &mother) {
        return from(BoardKindId::from(mother), mother.uniqueId);
    }
    static BoardGUID from(DaughterboardTopology const &daughter) {
        return from(BoardKindId::from(daughter), daughter.uniqueId);
    }
};

} // namespace irl::driver

// Hash support so the IDs can be used as unordered_map keys. Must be visible
// before LazyTopologyDB declares its unordered_map members.
namespace std {
template <> struct hash<irl::driver::BoardKindId> {
    std::size_t operator()(irl::driver::BoardKindId const &kind) const noexcept {
        std::size_t h = std::hash<uint8_t>{}(static_cast<uint8_t>(kind.boardClass));
        hash_combine(h, std::hash<uint32_t>{}(kind.hardwareRevision));
        hash_combine(h, std::hash<uint32_t>{}(static_cast<uint32_t>(kind.daughterKind)));
        return h;
    }
};

template <> struct hash<irl::driver::BoardGUID> {
    std::size_t operator()(irl::driver::BoardGUID const &guid) const noexcept {
        std::size_t h = std::hash<irl::driver::BoardKindId>{}(guid.boardKind);
        hash_combine(h, std::hash<uint64_t>{}(guid.uniqueId));
        return h;
    }
};
} // namespace std

namespace irl::driver {

// A database of all known topologies (patched for boards).
// Does not support updating topologies after insertion.
//
// Single-threaded only. Getters are const; the raw-JSON and parsed-template
// caches are mutable so builtin fallbacks can be cached lazily.
class LazyTopologyDB {
    // All kinds we know how to produce topologies for and their raw JSON
    // (needed for patching). If it's not here we don't know how to produce a
    // topology.
    // Well technically we special-case out the builtins so this has to be mutable
    mutable std::unordered_map<BoardKindId, nlohmann::json> mKnownKinds;
    // A cache which is a further parsed version of mKnownKinds. Un-patched
    // boards hit this.
    mutable std::unordered_map<BoardKindId, std::shared_ptr<const BoardTemplate>>
        mKindTemplateCache;

    // All known patches by GUID, stored as raw JSON text so parsing is deferred
    // until a board actually asks for its template. If it's not here it's not
    // getting applied.
    std::unordered_map<BoardGUID, std::string> mKnownPatches;
    // A cache of parsed boards by GUID.
    mutable std::unordered_map<BoardGUID, std::shared_ptr<const BoardTemplate>> mBoardTemplateCache;

    // Second. Checks mKnownKinds, otherwise falls back to
    // get_builtin_topology_for_kind. Caches the parsed builtin JSON on a
    // fallback hit.
    std::optional<std::reference_wrapper<const nlohmann::json>>
    get_json_for_kind(BoardKindId kind) const;
    // Third. Checks if we have a topology defined for a kind in
    // builtin_topos.hpp.
    std::optional<std::string_view> get_builtin_topology_for_kind(BoardKindId kind) const;

    // Shared implementation for get_template_for_kind. Returns null when the
    // kind is unknown.
    std::shared_ptr<const BoardTemplate> get_or_make_template_for_kind(BoardKindId kind) const;

  public:
    // An empty database. Builtin topologies are still available as fallbacks.
    LazyTopologyDB() = default;

    // Construct with some already-known kinds and patches.
    LazyTopologyDB(std::unordered_map<BoardKindId, nlohmann::json> knownKinds,
                   std::unordered_map<BoardGUID, std::string> knownPatches);

    // Registers a new kind. Asserts if the kind is already registered.
    void register_new_kind(BoardKindId kind, nlohmann::json json);

    // Registers a new patch. Asserts if the board GUID already has a patch.
    // The JSON overload serializes to text and forwards to the string overload
    // so there is only one storage path.
    void register_new_patch(BoardGUID guid, std::string patch);
    void register_new_patch(BoardGUID guid, nlohmann::json const &patch);

    // First. Checks mKindTemplateCache, otherwise falls back to
    // get_json_for_kind and parses the result. Caches on a hit.
    std::optional<std::reference_wrapper<const BoardTemplate>>
    get_template_for_kind(BoardKindId kind) const;

    // Checks mBoardTemplateCache, otherwise checks for a patch. If one is
    // registered for this board, applies it to get_json_for_kind, parses and
    // caches the result. If there is no patch, returns the kind template.
    std::optional<std::reference_wrapper<const BoardTemplate>>
    get_template_for_board(BoardGUID board) const;
};

} // namespace irl::driver
