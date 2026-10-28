#pragma once

// JSON -> C++ construction for board topology templates.
//
// nlohmann::json deliberately never reaches the public `include/` tree: this
// header lives under `src/` (a PRIVATE include path) and is compiled into
// `irldriver`, and it only forward-declares the JSON type. The constructor
// functions below turn a `nlohmann::json` object into the corresponding public
// `irl` type (e.g. `xbarPinIdFromJson`), so the rest of the codebase never has
// to touch JSON.
//
// Almost all string -> enum mapping is generated from the `VERT_KINDS` /
// `EDGE_KINDS` / `XBAR_PIN_SIDES` / `IRL_DAUGHTERBOARD_KINDS` x-macros, so
// adding a kind is enough to make its JSON spelling work.

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string_view>

#include <irl/command.hpp>
#include <irl/netlist.hpp>

#include "irl/driver/topology_db.hpp"

namespace irl::driver {

// Which of the two board classes a template describes.

struct ParseCtx {
    Netlist *netlist;

    NetlistVertexIdx addVertInner(std::string_view name, NetlistVertexKind kind,
                                  nlohmann::json const &vertJson);
    NetlistVertexIdx addPort(std::string_view name, nlohmann::json const &portJson);
    NetlistVertexIdx addComponent(std::string_view name, nlohmann::json const &compJson);
    NetlistEdgeIdx addPinAndNet(NetlistVertexIdx component_idx, nlohmann::json const &pinJson);
    NetlistVertexIdx getOrAddNet(std::string const &name);
};
// parses a board template out of raw json.
// Does not populate patchguid
// It is other software's responsibility to patch the json before this function
BoardTemplate parseBoardTemplate(nlohmann::json const &doc);

// name -> enum helpers

std::optional<XbarPinId::Side> XbarSideFromName(std::string_view name);
std::optional<DaughterboardKind> DaughterKindFromName(std::string_view name);
std::optional<BoardClass> BoardClassFromName(std::string_view name);

// ---- JSON constructors --------------------------------------------------

// Parse JSON with `//` line comments (and `/* */`) allowed, as used by
// topology templates.
nlohmann::json parseJsonc(std::string_view text);

uint32_t jsonToU32(nlohmann::json const &j);
int32_t jsonToI32(nlohmann::json const &j);
double jsonToDouble(nlohmann::json const &j);

XbarPinId xbarPinIdFromJson(nlohmann::json const &j);
SpstId spstIdFromJson(nlohmann::json const &j);

// Edge kind for one pin given it's attached to a component of kind componentKind
NetlistEdgeKind pinKindFromJson(NetlistVertexKind componentKind, nlohmann::json const &pinJson);

// Kind-specific pin payload ("data"); `kind` picks which alternative is read.
NetlistEdgeValue edgeDataFromJson(NetlistEdgeKind kind, nlohmann::json const &data);

// ---- Patching -----------------------------------------------------------

// Apply the RFC 6902 patch registered for `uniqueId` (keyed by its decimal
// spelling) in `registry`, if present. Missing registry/id is a no-op.
void applyJsonPatch(nlohmann::json &doc, uint64_t uniqueId, nlohmann::json const &registry);

// Convenience wrapper for a { "mother": {...}, "daughter": {...} } patch set:
// selects the registry for `klass`, then calls applyJsonPatch.
void applyBoardPatch(nlohmann::json &doc, BoardClass klass, uint64_t uniqueId,
                     nlohmann::json const &patches);

} // namespace irl::driver
