#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <irl/detail/smallvec.hpp>
#include <irl/detail/util.hpp>

namespace irl {

struct NoData {};

// Id for a motherboard. An index into the topology array
using MotherId_t = uint8_t;
static constexpr MotherId_t NO_MOTHERBOARD = std::numeric_limits<MotherId_t>::max();
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

// Type used as an index to address vertices and edges in a netlist
using netlistIdx_t = std::uint32_t;
using sNetlistIdx_t = std::int32_t;
inline constexpr netlistIdx_t NULL_INDEX = std::numeric_limits<netlistIdx_t>::max();

// A handle to a vertex slot inside a Netlist. It is deliberately just a u32, so
// it stays tiny and can be swapped/moved freely. NULL_INDEX is the null handle.
struct NetlistVertexIdx {
    netlistIdx_t value = NULL_INDEX;
    constexpr bool is_null() const { return value == NULL_INDEX; }
    friend constexpr bool operator==(NetlistVertexIdx, NetlistVertexIdx) = default;
};

// A handle to an edge slot inside a Netlist. See NetlistVertexIdx.
struct NetlistEdgeIdx {
    netlistIdx_t value = NULL_INDEX;
    constexpr bool is_null() const { return value == NULL_INDEX; }
    friend constexpr bool operator==(NetlistEdgeIdx, NetlistEdgeIdx) = default;
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
    // Holds the next free NetlistVertexIdx when a vertex is on the free list.
    NetlistVertexIdx freeNext;
};

// Result of parse.
// combinations:
//  number != null - parse successful
//  number == null && problematic_suffix.size()==0 - bad number
//  number == null && problematic_suffix.size()!=0 - bad suffix
struct NumericParseResult {
    std::optional<double> number;
    std::string_view problematic_suffix;
};
// Parse a number in the common spice format (suffixes + exps allowed)
NumericParseResult NumericParseCommon(std::string_view number);

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

// Identifies a pin on a crossbar
struct XbarPinId {
    enum class Side : uint8_t { ROW, COL };
    Side side;
    uint8_t index;
};

// A value held by an edge
union NetlistEdgeValue {
    XbarPinId xbarPinId;
    uint32_t pinIndex;
    NoData empty;
    // Holds the next free NetlistEdgeIdx when a vertex is on the free list.
    NetlistEdgeIdx freeNext;
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

class Netlist;

template <bool IsConst> class NetlistVertexT;
template <bool IsConst> class NetlistEdgeT;
template <bool IsConst, template <bool> typename ValT> class SoAIterT;

using NetlistVertex = NetlistVertexT<true>;
using MutNetlistVertex = NetlistVertexT<false>;
using NetlistEdge = NetlistEdgeT<true>;
using MutNetlistEdge = NetlistEdgeT<false>;

namespace detail {
// Struct-of-arrays storage for every vertex.
// size is a high-water mark, i.e. every slot in [0, size) is either live
// or a tombstone.
// Important: These must all be trivially relocatable (no internal ptrs)
using SmallStr = SmallVec<char, 24>;
using EdgeList = SmallVec<NetlistEdgeIdx, 5>;
struct NetlistVertexFields {
    NetlistVertexKind *mKind_arr = nullptr;
    SmallStr *mName_arr = nullptr;
    NetlistVertexValue *mValue_arr = nullptr;
    // Use 5 edges so all opamps can be stored in the inline storage.
    EdgeList *mEdges_arr = nullptr;

    sNetlistIdx_t size = 0;
    sNetlistIdx_t capacity = 0;
    NetlistVertexIdx freeHead{};
};
NetlistVertexFields *get_v_fields(Netlist *);
NetlistVertexFields const *get_v_fields(Netlist const *);
} // namespace detail

// A handle to a vertex. Holds only a pointer into a Netlist and a NetlistVertexIdx
// Only valid so long as the Netlist is not moved.
template <bool IsConst> class NetlistVertexT {
    using Fields = detail::NetlistVertexFields;
    using FieldsT = std::conditional_t<IsConst, const Fields, Fields>;
    using NetlistT = std::conditional_t<IsConst, const Netlist, Netlist>;

    NetlistT *mNetlist = nullptr;
    NetlistVertexIdx mIdx{};

    using ValueT = std::conditional_t<IsConst, const NetlistVertexValue, NetlistVertexValue>;

    using VertKindUnderlying = std::underlying_type_t<NetlistVertexKind>;
    static constexpr NetlistVertexKind DEAD =
        static_cast<NetlistVertexKind>(std::numeric_limits<VertKindUnderlying>::max());
    static constexpr FieldsT *(*GET_FIELDS_FN)(NetlistT *) = &detail::get_v_fields;

    // Private constructor. Only Netlist should be able to call this.
    NetlistVertexT(NetlistT &netlist, NetlistVertexIdx idx) : mNetlist(&netlist), mIdx(idx) {}

    template <bool> friend class NetlistVertexT;
    template <bool> friend class NetlistEdgeT;
    friend class Netlist;
    friend class SoAIterT<IsConst, NetlistVertexT>;

    inline FieldsT *fields() const { return detail::get_v_fields(mNetlist); }

  public:
    using IterT = SoAIterT<IsConst, NetlistVertexT>;

    NetlistVertexT() noexcept = default;

    // Mutable -> const conversion.
    NetlistVertexT(MutNetlistVertex const &other)
        requires(IsConst)
        : mNetlist(other.mNetlist), mIdx(other.mIdx) {}

    // Is this a dangling/invalid handle (null, out of range, or removed)?
    inline bool is_valid() const {
        if (mNetlist == nullptr || mIdx.is_null()) {
            return false;
        } else if (mIdx.value >= fields()->size) {
            return false;
        } else if (fields()->mKind_arr[mIdx.value] == DEAD) {
            return false;
        } else {
            return true;
        }
    }

#ifndef IRL_NETLIST_NO_CHECK
#define IRL_GET_CHECKED(FIELD)                                                                     \
    (detail::ASSERT(mNetlist != nullptr, "null netlist"),                                          \
     detail::ASSERT(!mIdx.is_null(), "null idx"),                                                  \
     detail::ASSERT(mIdx.value < fields()->size, "Idx too large"),                                 \
     detail::ASSERT(fields()->mKind_arr[mIdx.value] != DEAD, "DEAD"),                              \
     fields()->FIELD##_arr[mIdx.value])
#else
#define IRL_GET_CHECKED(FIELD) (mFields->FIELD##_arr[mIdx.value])
#endif

    inline NetlistVertexIdx idx() const { return mIdx; }

    inline NetlistVertexKind kind() const { return IRL_GET_CHECKED(mKind); }
    inline std::string_view name() const {
        auto const &mName = IRL_GET_CHECKED(mName);
        return std::string_view{mName.begin(), mName.sizeu()};
    }
    inline void rename(std::string_view new_name)
        requires(!IsConst)
    {
        detail::ASSERT(is_valid(), "Renamed invalid index.");
        mNetlist->rename_vertex(mIdx, new_name);
    }

    // If this vertex holds kind, return a pointer to its associated union field in
    // the netlist. Otherwise returns nullptr. The pointed-to type is const iff
    // this handle is const.
    template <NetlistVertexKind kind>
    VertKindInfo<kind>::AssociatedData *get_data_if() const
        requires(!IsConst);
    template <NetlistVertexKind kind>
    VertKindInfo<kind>::AssociatedData const *get_data_if() const
        requires(IsConst);

    inline std::span<NetlistEdgeIdx const> connections() const {
        const auto &edges = IRL_GET_CHECKED(mEdges);
        return {edges};
    }

    // The first connection of the given kind, or a null edge handle.
    NetlistEdgeT<IsConst> connection_matching(NetlistEdgeKind kind) const;

    // Reserves space for "npins" pins
    void reserve_pins(std::size_t npins)
        requires(!IsConst)
    {
        IRL_GET_CHECKED(mEdges).ensure_capacity(npins);
    }

    // Disconnects (and destroys) the edge at index i of connections()
    void disconnect_index(std::size_t index)
        requires(!IsConst);

    // Removes this vertex and all connections to it
    void remove_vertex()
        requires(!IsConst);

  private:
    // For netlist only
    inline detail::EdgeList *edges_mut()
        requires(!IsConst)
    {
        // Fine to do. array indexing returns an lvalue
        return &IRL_GET_CHECKED(mEdges);
    }
};

namespace detail {
struct NetlistEdgeFields {
    NetlistEdgeKind *mKind_arr = nullptr;
    // The component this edge is connected to
    NetlistVertexIdx *mComponent_arr = nullptr;
    // The net this edge is connected to
    NetlistVertexIdx *mNet_arr = nullptr;
    // Only meaningful for topologies.
    // The distance to the junction point of the net (nets assumed to have only one junction)
    double *mTraceLength_arr = nullptr;
    NetlistEdgeValue *mValue_arr = nullptr;

    sNetlistIdx_t size = 0;
    sNetlistIdx_t capacity = 0;
    NetlistEdgeIdx freeHead{};
};
NetlistEdgeFields *get_e_fields(Netlist *);
NetlistEdgeFields const *get_e_fields(Netlist const *);
} // namespace detail

// A handle to an edge. Holds only a pointer into Netlist and a NetlistEdgeIdx.
// Invalid as soon as that Netlist is moved
template <bool IsConst> class NetlistEdgeT {
    // Struct-of-arrays storage for edges
    using Fields = detail::NetlistEdgeFields;
    using FieldsT = std::conditional_t<IsConst, const Fields, Fields>;
    using NetlistT = std::conditional_t<IsConst, const Netlist, Netlist>;

    NetlistT *mNetlist = nullptr;
    NetlistEdgeIdx mIdx{};

    using ValueT = std::conditional_t<IsConst, const NetlistEdgeValue, NetlistEdgeValue>;

    using EdgeKindUnderlying = std::underlying_type_t<NetlistEdgeKind>;
    static constexpr NetlistEdgeKind DEAD =
        static_cast<NetlistEdgeKind>(std::numeric_limits<EdgeKindUnderlying>::max());
    static constexpr FieldsT *(*GET_FIELDS_FN)(NetlistT *) = &detail::get_e_fields;

    template <bool> friend class NetlistEdgeT;
    friend class Netlist;
    friend class SoAIterT<IsConst, NetlistEdgeT>;

    // Private constructor to be used by Netlist only
    NetlistEdgeT(NetlistT &netlist, NetlistEdgeIdx idx) : mNetlist(&netlist), mIdx(idx) {}

    inline FieldsT *fields() const { return detail::get_e_fields(mNetlist); }

  public:
    using IterT = SoAIterT<IsConst, NetlistEdgeT>;

    NetlistEdgeT() noexcept = default;

    // Mutable -> const conversion.
    NetlistEdgeT(const MutNetlistEdge &other)
        requires(IsConst)
        : mNetlist(other.mNetlist), mIdx(other.mIdx) {}

    inline bool is_valid() const {
        if (mNetlist == nullptr || mIdx.is_null()) {
            return false;
        } else if (mIdx.value >= fields()->size) {
            return false;
        } else if (fields()->mKind_arr[mIdx.value] == DEAD) {
            return false;
        } else {
            return true;
        }
    }

    inline NetlistEdgeIdx idx() const { return mIdx; }
    inline NetlistEdgeKind kind() const { return IRL_GET_CHECKED(mKind); }

    // The component this edge is connected to
    inline NetlistVertexT<IsConst> component() const {
        return {*mNetlist, IRL_GET_CHECKED(mComponent)};
    }
    // The net this edge is connected to
    inline NetlistVertexT<IsConst> net() const { return {*mNetlist, IRL_GET_CHECKED(mNet)}; }
    // The distance to the junction point of the net
    inline double trace_length() const { return IRL_GET_CHECKED(mTraceLength); }

    inline void set_trace_length(double length)
        requires(!IsConst)
    {
        IRL_GET_CHECKED(mTraceLength) = length;
    }

    template <NetlistEdgeKind kind>
    EdgeKindInfo<kind>::AssociatedData *get_data_if() const
        requires(!IsConst);
    template <NetlistEdgeKind kind>
    EdgeKindInfo<kind>::AssociatedData const *get_data_if() const
        requires(IsConst);
};

// Forward iterator over live vertex slots. Dereferences to a NetlistVertexT so
// the netlist's constness propagates to iteration.
template <bool IsConst, template <bool> typename ValT> class SoAIterT {
    using Val = ValT<IsConst>;
    using This = SoAIterT<IsConst, ValT>;
    using FieldsT = Val::FieldsT;
    using NetlistT = Val::NetlistT;

    NetlistT *mNetlist = nullptr;
    netlistIdx_t mIndex = 0;

    inline FieldsT *get_fields() { return Val::GET_FIELDS_FN(mNetlist); }

    void skip_dead() {
        while (mIndex < get_fields()->size && get_fields()->mKind_arr[mIndex] == Val::DEAD) {
            ++mIndex;
        }
    }

    friend class SoAIterT<true, ValT>;
    friend class Netlist;

    // Only to be used by netlist
    SoAIterT(NetlistT &netlist, netlistIdx_t index) noexcept : mNetlist(&netlist), mIndex(index) {
        skip_dead();
    }

  public:
    using value_type = ValT<IsConst>;
    using reference = value_type;
    using pointer = void;
    using difference_type = std::ptrdiff_t;
    using iterator_category = std::forward_iterator_tag;
    using iterator_concept = std::forward_iterator_tag;

    SoAIterT() noexcept = default;
    SoAIterT(const This &other) noexcept = default;
    SoAIterT(This &&other) noexcept = default;

    // mut -> const
    SoAIterT(const SoAIterT<false, ValT> &other)
        requires(IsConst)
        : mNetlist(other.mNetlist), mIndex(other.mIndex) {}

    value_type operator*() const { return value_type(*mNetlist, {mIndex}); }

    SoAIterT &operator++() {
        ++mIndex;
        skip_dead();
        return *this;
    }
    SoAIterT operator++(int) {
        SoAIterT copy = *this;
        ++*this;
        return copy;
    }

    bool operator==(const SoAIterT &other) const {
        return mNetlist == other.mNetlist && mIndex == other.mIndex;
    }
    bool operator!=(const SoAIterT &other) const { return !(*this == other); }
};

// A bipartite netlist graph stored as two struct-of-arrays pools. Vertices and
// edges are addressed by NetlistVertexIdx / NetlistEdgeIdx.
// These can be used as inputs to operator[] here, which yields a NetlistVertex / NetlistEdge.
class Netlist {
    // Helper classes used to manage hashing
    struct NamemapKey {
        NetlistVertexIdx i;
        std::string_view get(Netlist const *) const;
    };
    struct NamemapHash {
        Netlist const *mNetlist;
        using is_transparent = void; // Enables heterogeneous lookup
        size_t operator()(NamemapKey k) const;
        size_t operator()(std::string_view sv) const;
    };
    struct NamemapKeyEql {
        Netlist const *mNetlist;
        using is_transparent = void; // Enables heterogeneous lookup
        bool operator()(NamemapKey lhs, NamemapKey rhs) const;
        bool operator()(NamemapKey lhs, std::string_view rhs) const;
        bool operator()(std::string_view lhs, NamemapKey rhs) const;
    };

    NetlistVertex::Fields mVertices;
    NetlistEdge::Fields mEdges;

    std::unordered_set<NamemapKey, NamemapHash, NamemapKeyEql> mVertexNames;

    NetlistVertexIdx alloc_vertex_slot();
    NetlistEdgeIdx alloc_edge_slot();

    // Remove edge e from v's adjacency list (does not destroy the edge).
    void unlink_edge(NetlistVertexIdx v, NetlistEdgeIdx e);
    // Unlink e from both endpoints, tombstone it, and push it on the edge free list.
    void destroy_edge(NetlistEdgeIdx e);

    void destroy_all() noexcept;

    friend NetlistVertex;
    friend MutNetlistVertex;
    friend NetlistEdge;
    friend MutNetlistEdge;
    friend detail::NetlistVertexFields *detail::get_v_fields(Netlist *);
    friend detail::NetlistVertexFields const *detail::get_v_fields(Netlist const *);
    friend detail::NetlistEdgeFields *detail::get_e_fields(Netlist *);
    friend detail::NetlistEdgeFields const *detail::get_e_fields(Netlist const *);

  public:
    using VertexIter = NetlistVertex::IterT;
    using MutVertexIter = MutNetlistVertex::IterT;
    using EdgeIter = NetlistEdge::IterT;
    using MutEdgeIter = MutNetlistEdge::IterT;

    Netlist() noexcept
        : mVertices(), mEdges(), mVertexNames(0, NamemapHash{this}, NamemapKeyEql{this}) {};
    Netlist(const Netlist &) = delete;
    Netlist &operator=(const Netlist &) = delete;
    Netlist(Netlist &&other);
    Netlist &operator=(Netlist &&other);
    ~Netlist();

    // Ensure room for at least `n` vertices / edges. Useful for bulk adds.
    void reserve_vertices(sNetlistIdx_t n);
    void reserve_edges(sNetlistIdx_t n);

    // Create a new vertex in the netlist
    template <NetlistVertexKind newKind>
    NetlistVertexIdx new_vertex(std::string_view name,
                                VertKindInfo<newKind>::AssociatedData &&data);

    // Create a new net. Asserts that VertKindIsNet(kind)==true
    NetlistVertexIdx new_net(std::string_view name, NetlistVertexKind kind);

    // Fetch a vertex from this netlist by name
    std::optional<NetlistVertexIdx> get_vertex(std::string_view name) const;

    // Rename a vertex
    void rename_vertex(NetlistVertexIdx v, std::string_view new_name);

    // Connect to vertices
    template <NetlistEdgeKind KIND>
    NetlistEdgeIdx connect(NetlistVertexIdx v1, NetlistVertexIdx v2,
                           EdgeKindInfo<KIND>::AssociatedData &&data,
                           double len = std::numeric_limits<double>::signaling_NaN());

    // Removes a vertex and all connections to it
    void remove(NetlistVertexIdx idx);

    inline MutNetlistVertex operator[](NetlistVertexIdx idx) {
        return MutNetlistVertex(*this, idx);
    }
    inline NetlistVertex operator[](NetlistVertexIdx idx) const {
        return NetlistVertex(*this, idx);
    }
    inline MutNetlistEdge operator[](NetlistEdgeIdx idx) { return MutNetlistEdge(*this, idx); }
    inline NetlistEdge operator[](NetlistEdgeIdx idx) const { return NetlistEdge(*this, idx); }

    // Iteration:

    inline VertexIter vertices_begin() const { return VertexIter(*this, 0); }
    inline MutVertexIter vertices_begin() { return MutVertexIter(*this, 0); }
    inline VertexIter vertices_end() const { return VertexIter(*this, this->mVertices.size); }
    inline MutVertexIter vertices_end() { return MutVertexIter(*this, this->mVertices.size); }
    inline EdgeIter edges_begin() const { return EdgeIter(*this, 0); }
    inline MutEdgeIter edges_begin() { return MutEdgeIter(*this, 0); }
    inline EdgeIter edges_end() const { return EdgeIter(*this, this->mEdges.size); }
    inline MutEdgeIter edges_end() { return MutEdgeIter(*this, this->mEdges.size); }

    inline std::pair<VertexIter, VertexIter> vertices() const {
        return {vertices_begin(), vertices_end()};
    }
    inline std::pair<MutVertexIter, MutVertexIter> vertices() {
        return {vertices_begin(), vertices_end()};
    }
    std::pair<EdgeIter, EdgeIter> edges() const { return {edges_begin(), edges_end()}; }
    std::pair<MutEdgeIter, MutEdgeIter> edges() { return {edges_begin(), edges_end()}; }
};

//==============================
// Netlist inline accessors
//==============================

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

#define VERT_KIND(NAME, UNION_FIELD) VertKindInfo<NetlistVertexKind::NAME>::IsPort,
constexpr std::array VERT_IS_PORT_TABLE = {VERT_KINDS};
#undef VERT_KIND

#define EDGE_KIND(NAME, ASSOCIATED_VERT_KIND, UNION_FIELD)                                         \
    EdgeKindInfo<NetlistEdgeKind::NAME>::Name,
constexpr std::array EDGE_NAME_TABLE = {EDGE_KINDS};
#undef EDGE_KIND

// The vertex kind each edge kind attaches to, indexed by NetlistEdgeKind.
#define EDGE_KIND(NAME, ASSOCIATED_VERT_KIND, UNION_FIELD) NetlistVertexKind::ASSOCIATED_VERT_KIND,
constexpr std::array EDGE_ASSOCIATED_VERT_TABLE = {EDGE_KINDS};
#undef EDGE_KIND

inline NetlistVertexFields *get_v_fields(Netlist *n) { return &n->mVertices; }
inline NetlistVertexFields const *get_v_fields(Netlist const *n) { return &n->mVertices; }
inline NetlistEdgeFields *get_e_fields(Netlist *n) { return &n->mEdges; }
inline NetlistEdgeFields const *get_e_fields(Netlist const *n) { return &n->mEdges; }

}; // namespace detail

//=========================
// Basic utility functions
//=========================

inline constexpr bool VertKindIsNet(NetlistVertexKind kind) { return kind <= detail::LAST_NET; }

std::optional<NetlistVertexKind> VertKindFromName(std::string_view name);
inline constexpr std::string_view VertKindName(NetlistVertexKind kind) {
    return detail::VERT_NAME_TABLE[static_cast<size_t>(kind)];
}

inline constexpr bool VertKindIsPort(NetlistVertexKind kind) {
    return detail::VERT_IS_PORT_TABLE[static_cast<size_t>(kind)];
}

std::optional<NetlistEdgeKind> EdgeKindFromName(std::string_view name);
inline constexpr std::string_view EdgeKindName(NetlistEdgeKind kind) {
    return detail::EDGE_NAME_TABLE[static_cast<size_t>(kind)];
}

} // namespace irl

// Hash support so index handles can be used as map/set keys.
namespace std {
template <> struct hash<irl::NetlistVertexIdx> {
    std::size_t operator()(irl::NetlistVertexIdx idx) const noexcept {
        return std::hash<irl::netlistIdx_t>{}(idx.value);
    }
};
template <> struct hash<irl::NetlistEdgeIdx> {
    std::size_t operator()(irl::NetlistEdgeIdx idx) const noexcept {
        return std::hash<irl::netlistIdx_t>{}(idx.value);
    }
};
} // namespace std
