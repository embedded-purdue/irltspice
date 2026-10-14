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

    if (v1->get_kind() == NetlistVertexKind::NET) {
        detail::ASSERT(v2->get_kind() != NetlistVertexKind::NET, "Attempted to connect two nets.");
        net = v1;
        component = v2;
    } else {
        detail::ASSERT(v2->get_kind() == NetlistVertexKind::NET,
                       "Attempted to connect two components. Use a net.");
        net = v2;
        component = v1;
    }

    detail::ASSERT(component->get_kind() == EdgeKindInfo<KIND>::AssociatedVertKind,
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
