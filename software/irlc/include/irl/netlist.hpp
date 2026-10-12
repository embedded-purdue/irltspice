#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>

#include <irl/detail/smallvec.hpp>
#include <irl/detail/util.hpp>

namespace irl {

struct NoData {};

enum class NetKind {
    WIRE,
    CIR_INPUT,
    CIR_OUTPUT,
    V_GND,
    V_HIGH,
    V_NEG,
};

// A value associated with a vertex
// Which field is active depends on the kind of vertex
union NetlistVertexValue {
    double numeric;
    NetKind netKind;
    uint32_t customId;
    uint32_t xbarId;
    uint32_t spstId;
    struct {
        uint8_t motherId;
        uint8_t duaghterId;
    } motherToDaughterConnId;
    struct {
        uint8_t grandMotherId;
        uint8_t childMotherId;
    } motherToMotherConnId;
    NoData empty;
};

// What kind of vertex this is
// Each kind of vertex has a different associated value kind (see NetlistVertexValue)
enum class NetlistVertexKind {

// X-Macro pattern
// A name of a kind of vertex and which field of NetlistVertexValue is active for it
// Signature: VERT_KIND(NAME, UNION_FIELD)
#define VERT_KINDS                                                                                 \
    VERT_KIND(NET, netKind)                                                                        \
    VERT_KIND(R, numeric)                                                                          \
    VERT_KIND(C, numeric)                                                                          \
    VERT_KIND(OPAMP, empty)                                                                        \
    VERT_KIND(DIODE, empty)                                                                        \
    VERT_KIND(Q_PNP, empty)                                                                        \
    VERT_KIND(Q_NPN, empty)                                                                        \
    VERT_KIND(CUSTOM, customId)                                                                    \
    VERT_KIND(TOPO_XBAR, xbarId)                                                                   \
    VERT_KIND(TOPO_SPST, spstId)                                                                   \
    VERT_KIND(TOPO_MD_CONN, motherToDaughterConnId)                                                \
    VERT_KIND(TOPO_MM_CONN, motherToMotherConnId)

#define VERT_KIND(NAME, UNION_FIELD) NAME,

    VERT_KINDS

#undef VERT_KIND
};

// Info about a specific vertkind
template <NetlistVertexKind Kind> class VertKindInfo {};

#define VERT_KIND(NAME, UNION_FIELD)                                                               \
    template <> class VertKindInfo<NetlistVertexKind::NAME> {                                      \
      public:                                                                                      \
        /* The NetlistVertexValue type associated with this vertex kind */                         \
        using AssociatedData = decltype(NetlistVertexValue{}.UNION_FIELD);                         \
        static constexpr auto AssociatedField = &NetlistVertexValue::UNION_FIELD;                  \
    };
VERT_KINDS
#undef VERT_KIND

struct XbarPinId {
    enum class Side : uint8_t { ROW, COL };
    Side side;
    uint8_t index;
};

union NetlistEdgeValue {
    XbarPinId xbarPinId;
    uint32_t pinIndex;
    NoData empty;
};

enum class NetlistEdgeKind {

// X-Macro pattern
// A name of a kind of edge, which kind of vertex it's associated with
// Signature: EDGE_KIND(NAME, ASSOCIATED_VERT_KIND, UNION_FIELD)
#define EDGE_KINDS                                                                                 \
    EDGE_KIND(PIN_R, R, empty)                                                                     \
    EDGE_KIND(PIN_C, C, empty)                                                                     \
    EDGE_KIND(PIN_OPAMP_PLUS, OPAMP, empty)                                                        \
    EDGE_KIND(PIN_OPAMP_MINUS, OPAMP, empty)                                                       \
    EDGE_KIND(PIN_OPAMP_SUPPLY_PLUS, OPAMP, empty)                                                 \
    EDGE_KIND(PIN_OPAMP_SUPPLY_MINUS, OPAMP, empty)                                                \
    EDGE_KIND(PIN_OPAMP_OUT, OPAMP, empty)                                                         \
    EDGE_KIND(PIN_Q_PNP_BASE, Q_PNP, empty)                                                        \
    EDGE_KIND(PIN_Q_PNP_EMITTER, Q_PNP, empty)                                                     \
    EDGE_KIND(PIN_Q_PNP_COLLECTOR, Q_PNP, empty)                                                   \
    EDGE_KIND(PIN_Q_NPN_BASE, Q_NPN, empty)                                                        \
    EDGE_KIND(PIN_Q_NPN_EMITTER, Q_NPN, empty)                                                     \
    EDGE_KIND(PIN_Q_NPN_COLLECTOR, Q_NPN, empty)                                                   \
    EDGE_KIND(PIN_DIODE_K, DIODE, empty)                                                           \
    EDGE_KIND(PIN_DIODE_A, DIODE, empty)                                                           \
    EDGE_KIND(PIN_CUSTOM, CUSTOM, pinIndex)                                                        \
    EDGE_KIND(PIN_TOPO_XBAR, TOPO_XBAR, xbarPinId)                                                 \
    EDGE_KIND(PIN_TOPO_SPST, TOPO_SPST, empty)                                                     \
    EDGE_KIND(PIN_TOPO_MD_CONN, TOPO_MD_CONN, pinIndex)                                            \
    EDGE_KIND(PIN_TOPO_MM_CONN, TOPO_MM_CONN, pinIndex)

#define EDGE_KIND(NAME, ASSOCIATED_VERT_KIND, UNION_FIELD) NAME,

    EDGE_KINDS

#undef EDGE_KIND
};

// Info about a specific edgekind
template <NetlistEdgeKind Kind> class EdgeKindInfo {};

#define EDGE_KIND(NAME, ASSOCIATED_VERT_KIND, UNION_FIELD)                                         \
    template <> class EdgeKindInfo<NetlistEdgeKind::NAME> {                                        \
      public:                                                                                      \
        /* The NetlistVertexValue type associated with this vertex kind */                         \
        using AssociatedData = decltype(NetlistEdgeValue{}.UNION_FIELD);                           \
        constexpr static auto AssociatedField = &NetlistEdgeValue::UNION_FIELD;                    \
        static constexpr auto AssociatedVertKind = NetlistVertexKind::ASSOCIATED_VERT_KIND;        \
    };
EDGE_KINDS
#undef EDGE_KIND

// Forward-declare edge
class NetlistEdge;

// A vertex of the netlist
// To construct, use Netlist::new_vertex
class NetlistVertex {
  public:
    std::string mName;

  private:
    // Use 5 edges so all opamps can be stored in struct
    using EdgeList = detail::SmallVec<NetlistEdge *, 5>;

    NetlistVertexValue mValue;
    EdgeList mEdgeList;

    NetlistVertexKind mKind;

    // If you do this you guarantee assert the the active field of val matches kind
    NetlistVertex(std::string &&name, NetlistVertexKind kind, NetlistVertexValue &&val)
        : mName(std::move(name)), mValue(std::move(val)), mEdgeList{}, mKind(kind) {}

    // Hard to impl as constructors' template params can't be specified
    // template <NetlistVertexKind newKind>
    // NetlistVertex(std::string &&name, VertKindInfo<newKind>::AssociatedData &&data);

  public:
    inline NetlistVertexKind get_kind() const { return mKind; }

    // If this vertex holds kind, return a pointer to its associated union field in this struct
    // Otherwise returns nullptr
    template <NetlistVertexKind kind> VertKindInfo<kind>::AssociatedData *get_data_if();
    template <NetlistVertexKind kind> VertKindInfo<kind>::AssociatedData const *get_data_if() const;

    inline std::span<NetlistEdge const *const> connections() const {
        return {mEdgeList.begin(), mEdgeList.end()};
    }
    inline std::span<NetlistEdge *> connections() { return {mEdgeList.begin(), mEdgeList.end()}; }

    // Disconnects from index i of connections()
    void disconnect_index(size_t index);

    // Reserves space for "npins" pins
    inline void reserve_pins(size_t npins) { mEdgeList.ensure_capacity(npins); }

    NetlistEdge *connection_matching(NetlistEdgeKind);

    friend class Netlist;
    friend class NetlistEdge;
};

// An edge of the netlist
// Intended to be only const and basically POD
// Use the methods on mutable NetlistVertecies to modify this
class NetlistEdge {
  public:
    // The component this edge is connected to
    NetlistVertex *mComponent;
    // The net this edge is connected to
    NetlistVertex *mNet;
    // Only meaningful for topologies
    double mTraceLength;

  private:
    NetlistEdgeValue mValue;
    NetlistEdgeKind mKind;

    NetlistEdge(NetlistVertex *component, NetlistVertex *net, NetlistEdgeKind kind,
                NetlistEdgeValue &&val)
        : mComponent(component), mNet(net),
          mTraceLength(std::numeric_limits<double>::signaling_NaN()), mValue(std::move(val)),
          mKind(kind) {}

  public:
    inline NetlistEdgeKind get_kind() const { return mKind; }
    // If this vertex holds kind, return a pointer to its associated union field in this struct
    // Otherwise returns nullptr
    template <NetlistEdgeKind kind> EdgeKindInfo<kind>::AssociatedData const *get_data_if() const;

    friend class Netlist;
    friend class NetlistVertex;
};

class Netlist {
    struct VertexStore;
    struct EdgeStore;

    VertexStore *mVerticies;
    EdgeStore *mEdges;

    // Allocates a vertex
    // Points to uninitialized memory
    // Resource acquisition is NOT initialization
    NetlistVertex *alloc_vertex();
    // Allocates an edge
    // Points to uninitialized memory
    // Resource acquisition is NOT initialization
    NetlistEdge *alloc_edge();

  public:
    // Create a new vertex in the netlist
    template <NetlistVertexKind newKind>
    NetlistVertex *new_vertex(std::string &&name, VertKindInfo<newKind>::AssociatedData &&data) {
        NetlistVertex *loc = alloc_vertex();

        NetlistVertexValue val;
        new (&(val.*VertKindInfo<newKind>::AssociatedField))
            VertKindInfo<newKind>::AssociatedData{std::move(data)};

        new (loc) NetlistVertex(std::move(name), newKind, std::move(val));

        return loc;
    };

    template <NetlistEdgeKind KIND>
    NetlistEdge *connect(NetlistVertex *v1, NetlistVertex *v2,
                         EdgeKindInfo<KIND>::AssociatedData &&data) {

        detail::ASSERT(v1 != nullptr && v2 != nullptr, "Attempted to connect to a null vertex.");

        NetlistVertex *component;
        NetlistVertex *net;

        if (v1->get_kind() == NetlistVertexKind::NET) {
            detail::ASSERT(v2->get_kind() != NetlistVertexKind::NET,
                           "Attempted to connect two nets.");
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
};

} // namespace irl
