#include "irl/driver/topology_json.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "irl/command.hpp"
#include "irl/compiler/parse_info.hpp"
#include "irl/detail/util.hpp"
#include "irl/netlist.hpp"

namespace irl::driver {

using detail::ASSERT;

namespace {

template <typename> inline constexpr bool always_false = false;

// Names of the daughterboard kinds, generated from the x-macro in command.hpp.
constexpr std::array DAUGHTER_KIND_NAMES = {
#define IRL_DAUGHTERBOARD_KIND(NAME, VALUE)                                                        \
    std::pair{std::string_view(#NAME), DaughterboardKind::NAME},
    IRL_DAUGHTERBOARD_KINDS(IRL_DAUGHTERBOARD_KIND)
#undef IRL_DAUGHTERBOARD_KIND
};

// Payload parser for a single edge kind. Dispatches on the associated data
// type, so adding an edge kind with one of the existing payloads needs no new
// code here.
template <NetlistEdgeKind Kind>
EdgeKindInfo<Kind>::AssociatedData edgeDataImpl(nlohmann::json const *data) {
    using Data = typename EdgeKindInfo<Kind>::AssociatedData;
    if constexpr (std::is_same_v<Data, NoData>) {
        return NoData{};
    } else if constexpr (std::is_same_v<Data, uint32_t>) {
        return jsonToU32(data->at("index"));
    } else if constexpr (std::is_same_v<Data, XbarPinId>) {
        return xbarPinIdFromJson(*data);
    } else {
        static_assert(always_false<Data>, "Unsupported edge payload type");
    }
}

template <NetlistVertexKind Kind>
VertKindInfo<Kind>::AssociatedData vertDataImpl(nlohmann::json const &j) {
    using Data = typename VertKindInfo<Kind>::AssociatedData;

    if constexpr (VertKindInfo<Kind>::IsPort) {
        if constexpr (Kind == NetlistVertexKind::PORT_MD) {
            return {
                .mother_id = NO_MOTHERBOARD,
                .daughter_id = static_cast<DaughterId_t>(jsonToU32(j.at("daughter_id"))),
            };
        } else if constexpr (Kind == NetlistVertexKind::PORT_MM) {
            return {.grandmother_id = NO_MOTHERBOARD, .childmother_id = NO_MOTHERBOARD};
        } else {
            static_assert(Kind == NetlistVertexKind::PORT_GENERIC);
            return NoData{};
        }
    } else if constexpr (std::is_same_v<Data, NoData>) {
        return NoData{};
    } else if constexpr (std::is_same_v<Data, double>) {
        auto const &json = j.at("numeric");
        if (json.is_number()) {
            return json.get<double>();
        } else if (json.is_string()) {
            auto res = NumericParseCommon(j.at("numeric").get_ref<std::string const &>());
            IRL_ASSERT(res.number.has_value(), "Parse number failed.");
            return *res.number;
        } else {
            IRL_ASSERT(false, "Non-number/string for numeric");
            return std::numeric_limits<double>::signaling_NaN();
        }
    } else if constexpr (std::is_same_v<Data, XbarId>) {
        return XbarId{
            .ic_id = j.at("ic_id"),
            .daughter_id = NO_DAUGHTERBOARD,
            .mother_id = NO_MOTHERBOARD,
        };
    } else if constexpr (std::is_same_v<Data, SpstId>) {
        return SpstId{
            .sw_id = static_cast<uint8_t>(jsonToU32(j.at("sw_id"))),
            .ic_id = static_cast<uint8_t>(jsonToU32(j.at("ic_id"))),
            .daughter_id = NO_DAUGHTERBOARD,
            .mother_id = NO_MOTHERBOARD,
        };
    } else {
        static_assert(always_false<Data>, "Unsupported edge payload type");
    }
}

} // namespace

std::optional<XbarPinId::Side> XbarSideFromName(std::string_view name) {
    if (name == "ROW") {
        return XbarPinId::Side::ROW;
    } else if (name == "COL") {
        return XbarPinId::Side::COL;
    }
    return std::nullopt;
}

std::optional<DaughterboardKind> DaughterKindFromName(std::string_view name) {
    for (auto const &[kindName, kind] : DAUGHTER_KIND_NAMES) {
        if (kindName == name) {
            return kind;
        }
    }
    return std::nullopt;
}

std::optional<BoardClass> BoardClassFromName(std::string_view name) {
    if (name == "MOTHER") {
        return BoardClass::MOTHER;
    }
    if (name == "DAUGHTER") {
        return BoardClass::DAUGHTER;
    }
    return std::nullopt;
}

NetlistEdgeKind portPinKind(NetlistVertexKind portKind, std::string_view kindStr) {
    std::string name = "PIN_";
    name += VertKindName(portKind);
    name += "_";
    name += kindStr;
    auto kindEnum = EdgeKindFromName(name);
    IRL_ASSERT(kindEnum.has_value(), "No edge kind for this port side");
    return *kindEnum;
}

nlohmann::json parseJsonc(std::string_view text) {
    // allow_exceptions = true, ignore_comments = true
    return nlohmann::json::parse(text.begin(), text.end(), nullptr, true, true);
}

uint32_t jsonToU32(nlohmann::json const &j) {
    IRL_ASSERT(j.is_number(), "Expected a number.");
    return j.get<uint32_t>();
}

int32_t jsonToI32(nlohmann::json const &j) {
    IRL_ASSERT(j.is_number(), "Expected a number.");
    return j.get<int32_t>();
}

double jsonToDouble(nlohmann::json const &j) {
    IRL_ASSERT(j.is_number(), "Expected a number.");
    return j.get<double>();
}

XbarPinId xbarPinIdFromJson(nlohmann::json const &j) {
    auto side = XbarSideFromName(j.at("side").get<std::string>());
    IRL_ASSERT(side.has_value(), "Unknown crossbar pin side");

    XbarPinId out{};
    out.side = *side;
    uint32_t index = jsonToU32(j.at("index"));
    IRL_ASSERT(index <= 0xFF, "Crossbar pin index out of range");
    out.index = static_cast<uint8_t>(index);
    return out;
}

SpstId spstIdFromJson(nlohmann::json const &j) {
    SpstId out{};
    out.sw_id = static_cast<uint8_t>(jsonToU32(j.at("sw_id")));
    out.ic_id = static_cast<uint8_t>(jsonToU32(j.at("ic_id")));
    out.daughter_id = NO_DAUGHTERBOARD;
    out.mother_id = NO_MOTHERBOARD;
    return out;
}

NetlistVertexKind componentKindFromJson(nlohmann::json const &j) {
    auto kind = VertKindFromName(j.get<std::string>());
    IRL_ASSERT(kind.has_value(), "Unknown vertex kind");
    return *kind;
}

NetlistVertexKind portKindFromJson(nlohmann::json const &j) {
    std::string name = j.get<std::string>();
    if (!name.starts_with("PORT_")) {
        name = "PORT_" + name;
    }
    auto kind = VertKindFromName(name);
    IRL_ASSERT(kind.has_value() && VertKindIsPort(*kind), "Unknown port kind");
    return *kind;
}

NetlistEdgeKind pinKindFromJson(NetlistVertexKind componentKind, nlohmann::json const &pinJson) {
    std::string fullname = "PIN_";
    fullname += std::string(VertKindName(componentKind));
    if (pinJson.contains("kind")) {
        fullname += "_";
        fullname += pinJson.at("kind").get<std::string>();
    }
    auto kind = EdgeKindFromName(fullname);
    IRL_ASSERT(kind.has_value(), "Unknown pin edge kind");
    return *kind;
}

NetlistVertexIdx ParseCtx::getOrAddNet(std::string const &rawname) {
    auto kind = get_net_kind(rawname);
    auto name = get_net_canonical_name(kind, rawname);
    auto net_maybe = netlist->get_vertex(name);
    if (net_maybe.has_value()) {
        IRL_ASSERT(VertKindIsNet((*netlist)[*net_maybe].kind()), "Non net already has name.");
        return *net_maybe;
    } else {
        return netlist->new_net(name, get_net_kind(name));
    }
}

NetlistEdgeIdx ParseCtx::addPinAndNet(NetlistVertexIdx component_idx,
                                      nlohmann::json const &pinJson) {
    NetlistVertexIdx net_idx = getOrAddNet(pinJson.at("dest").get_ref<std::string const &>());

    NetlistEdgeKind kind = pinKindFromJson((*netlist)[component_idx].kind(), pinJson);
    double len = jsonToDouble(pinJson.at("len"));

    NetlistEdgeIdx idx;

    nlohmann::json const *datajson = pinJson.contains("data") ? &pinJson.at("data") : nullptr;

#define EDGE_KIND(NAME, A, UNION_FIELD)                                                            \
    case NetlistEdgeKind::NAME:                                                                    \
        idx = netlist->connect<NetlistEdgeKind::NAME>(                                             \
            component_idx, net_idx, edgeDataImpl<NetlistEdgeKind::NAME>(datajson), len);           \
        break;

    switch (kind) { EDGE_KINDS }

#undef EDGE_KIND

    return idx;
}

NetlistVertexIdx ParseCtx::addComponent(std::string_view name, nlohmann::json const &compJson) {

    NetlistVertexKind kind = componentKindFromJson(compJson.at("kind"));

    IRL_ASSERT(!VertKindIsPort(kind), "Added port in component list.");
    IRL_ASSERT(!VertKindIsNet(kind), "Added net in component list.");

    return addVertInner(name, kind, compJson);
}

NetlistVertexIdx ParseCtx::addPort(std::string_view name, nlohmann::json const &portJson) {

    NetlistVertexKind kind = portKindFromJson(portJson.at("kind"));

    IRL_ASSERT(VertKindIsPort(kind), "Added component in port list.");
    IRL_ASSERT(!VertKindIsNet(kind), "Added net in port list.");

    return addVertInner(name, kind, portJson);
}

NetlistVertexIdx ParseCtx::addVertInner(std::string_view name, NetlistVertexKind kind,
                                        nlohmann::json const &vertJson) {
    NetlistVertexIdx idx;

#define VERT_KIND(NAME, UNION_FIELD)                                                               \
    case NetlistVertexKind::NAME:                                                                  \
        idx = netlist->new_vertex<NetlistVertexKind::NAME>(                                        \
            name, vertDataImpl<NetlistVertexKind::NAME>(vertJson));                                \
        break;

    switch (kind) {
        VERT_KINDS
    default:
        IRL_ASSERT(false, "Bad kind. Could not add.");
    }

#undef VERT_KIND

    for (auto const &pinJson : vertJson.at("pins")) {
        addPinAndNet(idx, pinJson);
    }

    return idx;
}

BoardTemplate parseBoardTemplate(nlohmann::json const &doc) {

    BoardTemplate out;

    auto boardClass_o = BoardClassFromName(doc.at("class").get<std::string>());
    IRL_ASSERT(boardClass_o.has_value(), "Unknown board class");
    out.kind.boardClass = *boardClass_o;

    if (out.kind.boardClass == BoardClass::DAUGHTER) {
        IRL_ASSERT(doc.contains("kind") && !doc.at("kind").is_null(), "Missing daughterboard kind");
        auto kind = DaughterKindFromName(doc.at("kind").get<std::string>());
        IRL_ASSERT(kind.has_value(), "Unknown daughterboard kind");
        out.kind.daughterKind = *kind;
    }
    if (doc.contains("revision")) {
        out.kind.hardwareRevision = jsonToU32(doc.at("revision"));
    }

    auto const &topo = doc.at("topology");

    ParseCtx ctx{
        .netlist = &out.netlist,
    };

    for (auto const &[name, compJson] : topo.at("components").items()) {
        ctx.addComponent(name, compJson);
    }

    for (auto const &[name, portJson] : topo.at("ports").items()) {
        out.ports.push_back(ctx.addPort(name, portJson));
    }

    return out;
}

void applyJsonPatch(nlohmann::json &doc, uint64_t uniqueId, nlohmann::json const &registry) {
    if (!registry.is_object()) {
        return;
    }
    auto it = registry.find(std::to_string(uniqueId));
    if (it == registry.end()) {
        return;
    }
    doc = doc.patch(*it);
}

void applyBoardPatch(nlohmann::json &doc, BoardClass boardClass, uint64_t uniqueId,
                     nlohmann::json const &patches) {
    if (!patches.is_object()) {
        return;
    }
    auto it = patches.find(boardClass == BoardClass::MOTHER ? "mother" : "daughter");
    if (it == patches.end()) {
        return;
    }
    applyJsonPatch(doc, uniqueId, *it);
}

} // namespace irl::driver
