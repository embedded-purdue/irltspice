#include "irl/netlist.hpp"

using namespace irl;

// The dead-kind tombstone must never collide with a real (constructible) kind,
// or ChunkedList would treat a live element as a free slot.
#define VERT_KIND(NAME, UNION_FIELD)                                                               \
    static_assert(static_cast<detail::Chunkable<NetlistVertex>::VertKindInt>(                      \
                      NetlistVertexKind::NAME) != detail::Chunkable<NetlistVertex>::KIND_DEAD,     \
                  "NetlistVertexKind::" #NAME " collides with the dead-vertex tombstone");
VERT_KINDS
#undef VERT_KIND

#define EDGE_KIND(NAME, ASSOCIATED_VERT_KIND, UNION_FIELD)                                         \
    static_assert(static_cast<detail::Chunkable<NetlistEdge>::EdgeKindInt>(                        \
                      NetlistEdgeKind::NAME) != detail::Chunkable<NetlistEdge>::KIND_DEAD,         \
                  "NetlistEdgeKind::" #NAME " collides with the dead-edge tombstone");
EDGE_KINDS
#undef EDGE_KIND

template <NetlistVertexKind newKind>
NetlistVertex *Netlist::new_vertex(std::string &&name,
                                   VertKindInfo<newKind>::AssociatedData &&data) {
    NetlistVertex *loc = alloc_vertex();

    NetlistVertexValue val;
    new (&(val.*VertKindInfo<newKind>::AssociatedField))
        VertKindInfo<newKind>::AssociatedData{std::move(data)};

    new (loc) NetlistVertex(std::move(name), newKind, std::move(val));

    return loc;
}
// Explicitly instantiate
#define VERT_KIND(NAME, UNION_FIELD)                                                               \
    template NetlistVertex *Netlist::new_vertex<NetlistVertexKind::NAME>(                          \
        std::string &&, VertKindInfo<NetlistVertexKind::NAME>::AssociatedData &&);
VERT_KINDS
#undef VERT_KIND

template <NetlistEdgeKind KIND>
NetlistEdge *Netlist::connect(NetlistVertex *v1, NetlistVertex *v2,
                              EdgeKindInfo<KIND>::AssociatedData &&data) {

    detail::ASSERT(v1 != nullptr && v2 != nullptr, "Attempted to connect to a null vertex.");

    NetlistVertex *component;
    NetlistVertex *net;

    if (VertKindIsNet(v1->kind())) {
        detail::ASSERT(!VertKindIsNet(v2->kind()), "Attempted to connect two nets.");
        net = v1;
        component = v2;
    } else {
        detail::ASSERT(VertKindIsNet(v2->kind()),
                       "Attempted to connect two components. Use a net.");
        net = v2;
        component = v1;
    }

    detail::ASSERT(component->kind() == EdgeKindInfo<KIND>::AssociatedVertKind,
                   "Attempted to connect an edge to a non-matching component.");

    NetlistEdgeValue val;
    new (&(val.*EdgeKindInfo<KIND>::AssociatedField))
        EdgeKindInfo<KIND>::AssociatedData{std::move(data)};

    NetlistEdge *loc = alloc_edge();
    new (loc) NetlistEdge(component, net, KIND, std::move(val));

    net->mEdgeList.push_back(loc);
    component->mEdgeList.push_back(loc);

    return loc;
}
// Explicitly instantiate
#define EDGE_KIND(NAME, ASSOCIATED_VERT_KIND, UNION_FIELD)                                         \
    template NetlistEdge *Netlist::connect<NetlistEdgeKind::NAME>(                                 \
        NetlistVertex *, NetlistVertex *, EdgeKindInfo<NetlistEdgeKind::NAME>::AssociatedData &&);
EDGE_KINDS
#undef EDGE_KIND

// ==================
// get_data_if bodies
// ==================
// A vertex/edge only "holds" the union field that matches its kind. These
// return a pointer to that field when the kinds agree and nullptr otherwise,
// which is what makes the kindsafe accessor work.

template <NetlistVertexKind kind>
typename VertKindInfo<kind>::AssociatedData *NetlistVertex::get_data_if() {
    if (mKind != kind) {
        return nullptr;
    }
    return &(mValue.*VertKindInfo<kind>::AssociatedField);
}

template <NetlistVertexKind kind>
typename VertKindInfo<kind>::AssociatedData const *NetlistVertex::get_data_if() const {
    if (mKind != kind) {
        return nullptr;
    }
    return &(mValue.*VertKindInfo<kind>::AssociatedField);
}

// Explicitly instantiate
#define VERT_KIND(NAME, UNION_FIELD)                                                               \
    template VertKindInfo<NetlistVertexKind::NAME>::AssociatedData *                               \
    NetlistVertex::get_data_if<NetlistVertexKind::NAME>();                                         \
    template VertKindInfo<NetlistVertexKind::NAME>::AssociatedData const *                         \
    NetlistVertex::get_data_if<NetlistVertexKind::NAME>() const;
VERT_KINDS
#undef VERT_KIND

template <NetlistEdgeKind kind>
typename EdgeKindInfo<kind>::AssociatedData *NetlistEdge::get_data_if() {
    if (mKind != kind) {
        return nullptr;
    }
    return &(mValue.*EdgeKindInfo<kind>::AssociatedField);
}

template <NetlistEdgeKind kind>
typename EdgeKindInfo<kind>::AssociatedData const *NetlistEdge::get_data_if() const {
    if (mKind != kind) {
        return nullptr;
    }
    return &(mValue.*EdgeKindInfo<kind>::AssociatedField);
}

// Explicitly instantiate
#define EDGE_KIND(NAME, ASSOCIATED_VERT_KIND, UNION_FIELD)                                         \
    template EdgeKindInfo<NetlistEdgeKind::NAME>::AssociatedData *                                 \
    NetlistEdge::get_data_if<NetlistEdgeKind::NAME>();                                             \
    template EdgeKindInfo<NetlistEdgeKind::NAME>::AssociatedData const *                           \
    NetlistEdge::get_data_if<NetlistEdgeKind::NAME>() const;
EDGE_KINDS
#undef EDGE_KIND
