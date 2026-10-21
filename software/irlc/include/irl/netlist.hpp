#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <irl/detail/chunkedlist.hpp>
#include <irl/detail/smallvec.hpp>
#include <irl/detail/util.hpp>

namespace irl {

struct NoData {};

// Id for a motherboard. An index into the topology array
using MotherId_t = uint8_t;
// Id for a daughterboard, or NO_DAUGHTERBOARD (indicating just the motherboard).
// An index into corresponding motherboard's topology array
using DaughterId_t = uint8_t;
static constexpr DaughterId_t NO_DAUGHTERBOARD = std::numeric_limits<DaughterId_t>::max();

// An identifier for a topology SPST switch
// Effectively it's a hierarchical ID
// There are multiple boards, each board has multiple switch ICs, and each switch IC has multiple
// switches Each switch operates independently, though, so each needs a unique ID. The fields of
// this struct should not be touched, unless in advanced use cases
struct SpstId {
    // which switch on the ic is it?
    uint8_t sw_id : 8;
    // which ic on the board is it?
    uint8_t ic_id : 8;
    // Which daughterboard is this switch on
    // NO_DAUGHTERBOARD -> not on a daughterboard (on motherboard)
    DaughterId_t daughter_id : sizeof(DaughterId_t) * 8;
    // which motherboard is this switch a child of
    MotherId_t mother_id : sizeof(MotherId_t) * 8;
};

// An identifier for a topology crossbar switch
// Effectively it's a hierarchical ID
// There are multiple boards, each board has multiple crossbar ICs
// Each switch operates independently, though, so each needs a unique ID.
// The fields of this struct should not be touched, unless in advanced use cases
struct XbarId {
    // which ic on the board is it?
    uint16_t ic_id : 16;
    // Which daughterboard is this switch on
    // NO_DAUGHTERBOARD -> not on a daughterboard (on motherboard)
    DaughterId_t daughter_id : sizeof(DaughterId_t) * 8;
    // which motherboard is this switch a child of
    MotherId_t mother_id : sizeof(MotherId_t) * 8;
};

// A value associated with a vertex
// Which field is active depends on the kind of vertex
union NetlistVertexValue {
    double numeric;

    // An identifier for a topology crossbar
    // Unique across the entire topology
    XbarId xbarId;
    SpstId spstId;

    struct {
        MotherId_t mother_id;
        DaughterId_t daughter_id;
    } motherToDaughterPort;
    struct {
        MotherId_t grandmother_id;
        MotherId_t childmother_id;
    } motherToMotherPort;
    NoData empty;
    void *freeNext;
};

// What kind of vertex this is
// Each kind of vertex has a different associated value kind (see NetlistVertexValue)
enum class NetlistVertexKind : uint16_t {

// X-Macro pattern
// A name of a kind of vertex and which field of NetlistVertexValue is active for it
// Signature: VERT_KIND(NAME, UNION_FIELD)
// A vertex is either a NET, a COMPONENT, or a PORT
// If it is a net there are 3 kinds:
//   1. NET - Generic NET.Any NET in a design can be mapped to any NET in a topology
//   2. NET_V_?? - Constant voltage.
//      Any constant voltage in a design may be mapped to any constant voltage in the topology with
//      matching kind. The rule verifier knows what these voltages are, and will attempt to prevent
//      short-circuits. There are only 3 voltages on the board: +/-5V and GND.
//   3. NET_SPECIAL - Special net.
//      A design NET_SPECIAL will be mapped to a topology NET_SPECIAL iff their names match.
//      These are intended to be used as an escape hatch for unknowable signals (i.e. circuit input
//      or output or custom components). In a user's design, this must be prefixed with "IRLS_"
// If it is a COMPONENT, it's a normal electrical component (R,C, opamp, etc) or a topology
// component
//    Topology components may not appear in designs. They are special because the router knows what
//    they do.
// If it is a PORT it is known to just electrically bridge two nets, except that it leaves a given
// PCB. A PORT has two sides: parent and child, and bridges two nets on different boards.
//    There are 3 flavors of PORTS:
//      - PORT_GENERIC - a 1-to-1 connection point on the PCB a user may interact with.
//          Can appear on mother or daughter. Using CLI flags/some interface, the user may specify
//          what net is connected to the child side. The parent side is always the board it appears
//          on. A user might add a flag like:
//              --port-connection={"mother":0,"daughter":1,"port":"PORT_NAME1","net":"IRLS_CIRCUIT_INPUT"}
//          This allows the user to connect arbitrary nets to a PORT_GENERIC only. Note the IRLS_
//          prefix; if that predix is not specified, it will be assumed that this is a normal net,
//          i.e. two flags representing a jumper wire bridging two boards. If no flag is specified
//          it is assumed the port is disconnected, and nets connected to parent side may be used as
//          normal design nets.
//      - PORT_MM - An N-to-N bundle of nets connecting a GRAND-motherboard to a motherboard.
//          Can only appear on a mother. A mother's topology may have at most 2:
//          one which it is connected to the child side and one where it is connected to the parent
//          side. Ids will always be overwritten. The parent MM port of motherboard i in the
//          topology's motherboards array will be joined with the child MM port of motherboard i+1.
//          Connects nets connected to matching "pinIndex"s.
//      - PORT_MD - An N-to-N bundle of nets connecting a motherboard to a daughterboard.
//          May appear on a mother any number of times, given each has a distinct daughter_id.
//          Can appear only once on a daughter. When stitching, a daughterboard in port i of a
//          motherboard will have its topology instantiated and connected to PORT_MD with
//          daughter_id "i" on the motherboard's topology. The mother id will be overwritten by the
//          index of the mother. Connects nets connected to matching "pinIndex"s.
// Other notes on topology creation:
//  - Nets on mother/daughters will be prefixed by "M{m}D{d}:" to uniquify their names.
//      - Does not apply to special nets or nets specified to connect to PORT_GENERICs.
//  - Nets are assumed to be star-shaped junctions (i.e. just joined at one point) to make edge
//  length meaningful.
#define VERT_KINDS                                                                                 \
    VERT_KIND(NET, empty)                                                                          \
    VERT_KIND(NET_SPECIAL, empty)                                                                  \
    VERT_KIND(NET_V_GND, empty)                                                                    \
    VERT_KIND(NET_V_HIGH, empty)                                                                   \
    VERT_KIND(NET_V_NEG, empty)                                                                    \
    VERT_KIND(R, numeric)                                                                          \
    VERT_KIND(C, numeric)                                                                          \
    VERT_KIND(OPAMP, empty)                                                                        \
    VERT_KIND(DIODE, empty)                                                                        \
    VERT_KIND(Q_PNP, empty)                                                                        \
    VERT_KIND(Q_NPN, empty)                                                                        \
    VERT_KIND(TOPO_XBAR, xbarId)                                                                   \
    VERT_KIND(TOPO_SPST, spstId)                                                                   \
    VERT_KIND(PORT_GENERIC, empty)                                                                 \
    VERT_KIND(PORT_MM, motherToMotherPort)                                                         \
    VERT_KIND(PORT_MD, motherToDaughterPort)

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
        static constexpr std::string_view Name = #NAME;                                            \
        static constexpr bool IsNet = Name.starts_with("NET");                                     \
        static constexpr bool IsPort = Name.starts_with("PORT");                                   \
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
    EDGE_KIND(PIN_TOPO_XBAR, TOPO_XBAR, xbarPinId)                                                 \
    EDGE_KIND(PIN_TOPO_SPST, TOPO_SPST, empty)                                                     \
    EDGE_KIND(PIN_PORT_GENERIC_PARENT, PORT_GENERIC, empty)                                        \
    EDGE_KIND(PIN_PORT_GENERIC_CHILD, PORT_GENERIC, empty)                                         \
    EDGE_KIND(PIN_PORT_MM_PARENT, PORT_MM, pinIndex)                                               \
    EDGE_KIND(PIN_PORT_MM_CHILD, PORT_MM, pinIndex)                                                \
    EDGE_KIND(PIN_PORT_MD_PARENT, PORT_MD, pinIndex)                                               \
    EDGE_KIND(PIN_PORT_MD_CHILD, PORT_MD, pinIndex)

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
        static constexpr std::string_view Name = #NAME;                                            \
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
    inline NetlistVertexKind kind() const { return mKind; }
    inline std::string_view name() const { return mName; }

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

    // Removes a vertex and all connections to it
    void remove_vertex(NetlistVertex *vert);

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
    // Only meaningful for topologies.
    // The distance to the junction point of the net (nets assumed to have only one junction)
    double mTraceLength = std::numeric_limits<double>::signaling_NaN();

  private:
    NetlistEdgeValue mValue{.empty{}};
    NetlistEdgeKind mKind;

    NetlistEdge(NetlistVertex *component, NetlistVertex *net, NetlistEdgeKind kind,
                NetlistEdgeValue &&val)
        : mComponent(component), mNet(net), mValue(std::move(val)), mKind(kind) {}

  public:
    inline NetlistEdgeKind kind() const { return mKind; }

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

constexpr NetlistVertexKind LAST_NET = NetlistVertexKind::NET_V_NEG;
#define VERT_KIND(NAME, UNION_FIELD)                                                               \
    (VertKindInfo<NetlistVertexKind::NAME>::IsNet == NetlistVertexKind::NAME <= LAST_NET) &&
static_assert(VERT_KINDS true, "LAST_NET is incorrect.");
#undef VERT_KIND

#define VERT_KIND(NAME, UNION_FIELD) VertKindInfo<NetlistVertexKind::NAME>::IsNet,
constexpr std::array VERT_IS_NET_TABLE = {VERT_KINDS};
#undef VERT_KIND

#define VERT_KIND(NAME, UNION_FIELD) VertKindInfo<NetlistVertexKind::NAME>::Name,
constexpr std::array VERT_NAME_TABLE = {VERT_KINDS};
#undef VERT_KIND

#define EDGE_KIND(NAME, ASSOCIATED_VERT_KIND, UNION_FIELD)                                         \
    EdgeKindInfo<NetlistEdgeKind::NAME>::Name,
constexpr std::array EDGE_NAME_TABLE = {EDGE_KINDS};
#undef EDGE_KIND

}; // namespace detail

//=========================
// Basic utility functions
//=========================

inline constexpr bool VertKindIsNet(NetlistVertexKind kind) { return kind <= detail::LAST_NET; }

inline constexpr std::string_view VertKindName(NetlistVertexKind kind) {
    return detail::VERT_NAME_TABLE[static_cast<size_t>(kind)];
}

inline constexpr std::string_view EdgeKindName(NetlistEdgeKind kind) {
    return detail::EDGE_NAME_TABLE[static_cast<size_t>(kind)];
}

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
