#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

#include <irl/detail/chunkedlist.hpp>
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
    void *freeNext;
};

// What kind of vertex this is
// Each kind of vertex has a different associated value kind (see NetlistVertexValue)
enum class NetlistVertexKind : uint16_t {

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
  private:
    // Use 5 edges so all opamps can be stored in struct
    using EdgeList = detail::SmallVec<NetlistEdge *, 5>;

    EdgeList mEdgeList{};
    std::string mName{};
    NetlistVertexValue mValue{.empty{}};
    // Chunkable<NetlistVertex> uses the max value as a tombstone.
    NetlistVertexKind mKind;

    // If you do this you guarantee assert the the active field of val matches kind
    NetlistVertex(std::string &&name, NetlistVertexKind kind, NetlistVertexValue &&val)
        : mEdgeList{}, mName(std::move(name)), mValue(std::move(val)), mKind(kind) {}

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
    friend class detail::Chunkable<NetlistVertex>;
};

// An edge of the netlist
// Intended to be only const and basically POD
// Use the methods on mutable NetlistVertecies to modify this
class NetlistEdge {
  public:
    // The component this edge is connected to
    NetlistVertex *mComponent = nullptr;
    // The net this edge is connected to
    NetlistVertex *mNet = nullptr;
    // Only meaningful for topologies
    double mTraceLength = std::numeric_limits<double>::signaling_NaN();

  private:
    NetlistEdgeValue mValue{.empty{}};
    NetlistEdgeKind mKind;

    NetlistEdge(NetlistVertex *component, NetlistVertex *net, NetlistEdgeKind kind,
                NetlistEdgeValue &&val)
        : mComponent(component), mNet(net), mValue(std::move(val)), mKind(kind) {}

  public:
    inline NetlistEdgeKind get_kind() const { return mKind; }
    // If this vertex holds kind, return a pointer to its associated union field in this struct
    // Otherwise returns nullptr
    template <NetlistEdgeKind kind> EdgeKindInfo<kind>::AssociatedData const *get_data_if() const;
    template <NetlistEdgeKind kind> EdgeKindInfo<kind>::AssociatedData *get_data_if();

    friend class Netlist;
    friend class NetlistVertex;
    friend class detail::Chunkable<NetlistEdge>;
};

class Netlist {
    static constexpr std::size_t VERT_CHUNK_SIZE = 20;
    static constexpr std::size_t EDGE_CHUNK_SIZE = 20;
    using VertexList = detail::ChunkedList<NetlistVertex, VERT_CHUNK_SIZE>;
    using EdgeList = detail::ChunkedList<NetlistEdge, EDGE_CHUNK_SIZE>;
    VertexList mVertices;
    EdgeList mEdges;

    // Allocates a vertex: raw storage; the caller placement-news.
    // Resource acquisition is NOT initialization.
    NetlistVertex *alloc_vertex() { return mVertices.allocate(); }
    // Allocates an edge: raw storage; the caller placement-news.
    // Resource acquisition is NOT initialization.
    NetlistEdge *alloc_edge() { return mEdges.allocate(); }

  public:
    // Create a new vertex in the netlist
    template <NetlistVertexKind newKind>
    NetlistVertex *new_vertex(std::string &&name, VertKindInfo<newKind>::AssociatedData &&data);

    // Connect to vertices
    template <NetlistEdgeKind KIND>
    NetlistEdge *connect(NetlistVertex *v1, NetlistVertex *v2,
                         EdgeKindInfo<KIND>::AssociatedData &&data);

    // Iteration:

    using VertexIter = VertexList::Iter;
    using ConstVertexIter = VertexList::ConstIter;
    using EdgeIter = EdgeList::Iter;
    using ConstEdgeIter = EdgeList::ConstIter;

    ConstVertexIter vertices_begin() const { return mVertices.begin(); }
    VertexIter vertices_begin() { return mVertices.begin(); }
    ConstEdgeIter edges_begin() const { return mEdges.begin(); }
    EdgeIter edges_begin() { return mEdges.begin(); }

    std::pair<ConstVertexIter, ConstVertexIter> vertices() const {
        return {mVertices.begin(), mVertices.end()};
    }
    std::pair<VertexIter, VertexIter> vertices() { return {mVertices.begin(), mVertices.end()}; }

    std::pair<ConstEdgeIter, ConstEdgeIter> edges() const { return {mEdges.begin(), mEdges.end()}; }
    std::pair<EdgeIter, EdgeIter> edges() { return {mEdges.begin(), mEdges.end()}; }
};

namespace detail {
// Free list lives in NetlistVertexValue::freeNext; the tombstone is mKind.
// Both hooks must work on uninitialized storage (see chunkedlist.hpp).
template <> class Chunkable<NetlistVertex> {
    template <typename, std::size_t, typename> friend class ChunkedList;

  public:
    using VertKindInt = std::underlying_type_t<NetlistVertexKind>;
    static constexpr VertKindInt KIND_DEAD = std::numeric_limits<VertKindInt>::max();

  private:
    static bool is_dead(const NetlistVertex &v) {
        return static_cast<VertKindInt>(v.mKind) == KIND_DEAD;
    }
    static void mark_dead(NetlistVertex &v) { v.mKind = static_cast<NetlistVertexKind>(KIND_DEAD); }
    static NetlistVertex *get_freelist(const NetlistVertex &v) {
        return static_cast<NetlistVertex *>(v.mValue.freeNext);
    }
    static void set_freelist(NetlistVertex &v, NetlistVertex *next) { v.mValue.freeNext = next; }
};

// The component/net pointers are meaningless while an edge is free, so we reuse
// mComponent to thread the free list. The tombstone is mKind.
template <> class Chunkable<NetlistEdge> {
    template <typename, std::size_t, typename> friend class ChunkedList;

  public:
    using EdgeKindInt = std::underlying_type_t<NetlistEdgeKind>;
    static constexpr EdgeKindInt KIND_DEAD = std::numeric_limits<EdgeKindInt>::max();

  private:
    static bool is_dead(const NetlistEdge &e) {
        return static_cast<EdgeKindInt>(e.mKind) == KIND_DEAD;
    }
    static void mark_dead(NetlistEdge &e) { e.mKind = static_cast<NetlistEdgeKind>(KIND_DEAD); }
    static NetlistEdge *get_freelist(const NetlistEdge &e) {
        return reinterpret_cast<NetlistEdge *>(e.mComponent);
    }
    static void set_freelist(NetlistEdge &e, NetlistEdge *next) {
        e.mComponent = reinterpret_cast<NetlistVertex *>(next);
    }
};
} // namespace detail

} // namespace irl
