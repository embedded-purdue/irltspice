#include "irl/netlist.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <utility>

#include "irl/detail/smallvec.hpp"

using namespace irl;
using detail::EdgeList;
using detail::SmallStr;
using detail::SmallVec;

#define VERTEX_FIELDS                                                                              \
    FIELD(NetlistVertexKind, mKind_arr)                                                            \
    FIELD(SmallStr, mName_arr)                                                                     \
    FIELD(NetlistVertexValue, mValue_arr)                                                          \
    FIELD(EdgeList, mEdges_arr)

#define EDGE_FIELDS                                                                                \
    FIELD(NetlistEdgeKind, mKind_arr)                                                              \
    FIELD(NetlistVertexIdx, mComponent_arr)                                                        \
    FIELD(NetlistVertexIdx, mNet_arr)                                                              \
    FIELD(double, mTraceLength_arr)                                                                \
    FIELD(NetlistEdgeValue, mValue_arr)

void free_vertex_fields(detail::NetlistVertexFields &v) noexcept {
#define FIELD(T, N) std::free(v.N);
    VERTEX_FIELDS
#undef FIELD
}
void free_edge_fields(detail::NetlistEdgeFields &e) noexcept {
#define FIELD(T, N) std::free(e.N);
    EDGE_FIELDS
#undef FIELD
}

#define VERT_DEAD (NetlistVertex::DEAD)
#define EDGE_DEAD (NetlistEdge::DEAD)

// =================
// Lifetime
// =================

Netlist::~Netlist() { destroy_all(); }

Netlist::Netlist(Netlist &&other) noexcept : mVertices(other.mVertices), mEdges(other.mEdges) {
    other.mVertices = {};
    other.mEdges = {};
}

Netlist &Netlist::operator=(Netlist &&other) noexcept {
    if (this != &other) {
        destroy_all();
        mVertices = other.mVertices;
        mEdges = other.mEdges;
        other.mVertices = {};
        other.mEdges = {};
    }
    return *this;
}

// Destroy every live element and release all column storage.
void Netlist::destroy_all() noexcept {
#define FIELD(T, N) std::destroy_at(arr.N + i);
    {
        auto arr = mVertices;
        for (netlistIdx_t i = 0; i < mVertices.size; ++i) {
            if (mVertices.mKind_arr[i] != VERT_DEAD) {
                VERTEX_FIELDS
            }
        }
    }
    auto arr = mEdges;
    for (netlistIdx_t i = 0; i < mEdges.size; ++i) {
        if (mEdges.mKind_arr[i] != EDGE_DEAD) {
            EDGE_FIELDS
        }
    }
#undef FIELD
    free_vertex_fields(mVertices);
    free_edge_fields(mEdges);
    mVertices = {};
    mEdges = {};
}

void Netlist::reserve_vertices(sNetlistIdx_t newcap) {
    auto &verts = mVertices;
    if (newcap <= verts.capacity) {
        return;
    }
#define FIELD(T, N) verts.N = static_cast<T *>(std::realloc(verts.N, sizeof(T) * newcap));
    VERTEX_FIELDS
#undef FIELD
    verts.capacity = newcap;
}

void Netlist::reserve_edges(sNetlistIdx_t newcap) {
    auto &edges = mEdges;
    if (newcap <= edges.capacity) {
        return;
    }
#define FIELD(T, N) edges.N = static_cast<T *>(std::realloc(edges.N, sizeof(T) * newcap));
    EDGE_FIELDS
#undef FIELD
    edges.capacity = newcap;
}

#define MINIMUM_VERT_AMT 32
#define MINIMUM_EDGE_AMT 32

// Slot acquisition order: append while there is fresh room, then reuse the free
// list, then grow. `size` is a high-water mark and never decreases.
NetlistVertexIdx Netlist::alloc_vertex_slot() {
    if (mVertices.size < mVertices.capacity) {
        std::cout << "got room!\n";
        return NetlistVertexIdx{static_cast<netlistIdx_t>(mVertices.size++)};
    } else if (!mVertices.freeHead.is_null()) {
        NetlistVertexIdx idx = mVertices.freeHead;
        mVertices.freeHead = NetlistVertexIdx{mVertices.mValue_arr[idx.value].freeNext};
        return idx;
    } else {
        std::cout << "no room, reserving!\n";
        reserve_vertices(mVertices.capacity == 0 ? MINIMUM_VERT_AMT : mVertices.capacity * 2);
        return NetlistVertexIdx{static_cast<netlistIdx_t>(mVertices.size++)};
    }
}

NetlistEdgeIdx Netlist::alloc_edge_slot() {
    if (mEdges.size < mEdges.capacity) {
        return NetlistEdgeIdx{static_cast<netlistIdx_t>(mEdges.size++)};
    }
    if (!mEdges.freeHead.is_null()) {
        NetlistEdgeIdx idx = mEdges.freeHead;
        mEdges.freeHead = NetlistEdgeIdx{mEdges.mValue_arr[idx.value].freeNext};
        return idx;
    }
    reserve_edges(mEdges.capacity == 0 ? MINIMUM_EDGE_AMT : mEdges.capacity * 2);
    return NetlistEdgeIdx{static_cast<netlistIdx_t>(mEdges.size++)};
}

//=======================
// Initialization Helpers
//=======================

#define FIELD(T, N) , T &&N##_val
static inline void init_vert(detail::NetlistVertexFields *verts, NetlistVertexIdx i VERTEX_FIELDS) {
#undef FIELD

#define FIELD(T, N) new (verts->N + i.value) T(std::move(N##_val));
    VERTEX_FIELDS
#undef FIELD
}

#define FIELD(T, N) , T &&N##_val
static inline void init_edge(detail::NetlistEdgeFields *edges, NetlistEdgeIdx i EDGE_FIELDS) {
#undef FIELD

#define FIELD(T, N) new (edges->N + i.value) T(std::move(N##_val));
    EDGE_FIELDS
#undef FIELD
}

// ==================
// get_data_if bodies
// ==================
// A vertex/edge only "holds" the union field that matches its kind. These
// return a pointer to that field when the kinds agree and nullptr otherwise,
// which is what makes the kindsafe accessor work.

template <bool IsConst>
template <NetlistVertexKind kind>
VertKindInfo<kind>::AssociatedData *NetlistVertexT<IsConst>::get_data_if() const
    requires(!IsConst)
{
    if (this->kind() != kind) {
        return nullptr;
    }
    return &(IRL_GET_CHECKED(mValue).*VertKindInfo<kind>::AssociatedField);
}

template <bool IsConst>
template <NetlistVertexKind kind>
VertKindInfo<kind>::AssociatedData const *NetlistVertexT<IsConst>::get_data_if() const
    requires(IsConst)
{
    if (this->kind() != kind) {
        return nullptr;
    }
    return &(IRL_GET_CHECKED(mValue).*VertKindInfo<kind>::AssociatedField);
}

// Explicitly instantiate
#define VERT_KIND(NAME, UNION_FIELD)                                                               \
    template VertKindInfo<NetlistVertexKind::NAME>::AssociatedData *                               \
    NetlistVertexT<false>::get_data_if<NetlistVertexKind::NAME>() const;                           \
    template VertKindInfo<NetlistVertexKind::NAME>::AssociatedData const *                         \
    NetlistVertexT<true>::get_data_if<NetlistVertexKind::NAME>() const;

VERT_KINDS
#undef VERT_KIND

// Same thing for edges

template <bool IsConst>
template <NetlistEdgeKind kind>
EdgeKindInfo<kind>::AssociatedData *NetlistEdgeT<IsConst>::get_data_if() const
    requires(!IsConst)
{
    if (this->kind() != kind) {
        return nullptr;
    }
    return &(IRL_GET_CHECKED(mValue).*EdgeKindInfo<kind>::AssociatedField);
}
template <bool IsConst>
template <NetlistEdgeKind kind>
EdgeKindInfo<kind>::AssociatedData const *NetlistEdgeT<IsConst>::get_data_if() const
    requires(IsConst)
{
    if (this->kind() != kind) {
        return nullptr;
    }
    return &(IRL_GET_CHECKED(mValue).*EdgeKindInfo<kind>::AssociatedField);
}

// Explicitly instantiate
#define EDGE_KIND(NAME, ASSOCIATED_VERT_KIND, UNION_FIELD)                                         \
    template EdgeKindInfo<NetlistEdgeKind::NAME>::AssociatedData *                                 \
    NetlistEdgeT<false>::get_data_if<NetlistEdgeKind::NAME>() const;                               \
    template EdgeKindInfo<NetlistEdgeKind::NAME>::AssociatedData const *                           \
    NetlistEdgeT<true>::get_data_if<NetlistEdgeKind::NAME>() const;

EDGE_KINDS
#undef EDGE_KIND

// =================

template <NetlistVertexKind newKind>
NetlistVertexIdx Netlist::new_vertex(std::string_view name,
                                     VertKindInfo<newKind>::AssociatedData &&data) {
    // Alloc a slot
    NetlistVertexIdx idx = alloc_vertex_slot();
    std::cout << "Newing vert: " << name << " at idx: " << idx.value
              << " with sz:" << mVertices.size << "\n";
    // Initialize the vertex but leave the union just default initialized
    init_vert(&mVertices, idx, newKind, SmallStr{name.begin(), name.end()}, {}, EdgeList{});
    // And initialize the enum in place later
    auto loc = mVertices.mValue_arr + idx.value;
    new (&(loc->*VertKindInfo<newKind>::AssociatedField))
        VertKindInfo<newKind>::AssociatedData(std::move(data));
    return idx;
}
// Explicitly instantiate
#define VERT_KIND(NAME, UNION_FIELD)                                                               \
    template NetlistVertexIdx Netlist::new_vertex<NetlistVertexKind::NAME>(                        \
        std::string_view, VertKindInfo<NetlistVertexKind::NAME>::AssociatedData &&);
VERT_KINDS
#undef VERT_KIND

template <NetlistEdgeKind KIND>
NetlistEdgeIdx Netlist::connect(NetlistVertexIdx v1_i, NetlistVertexIdx v2_i,
                                EdgeKindInfo<KIND>::AssociatedData &&data) {
    MutNetlistVertex v1 = (*this)[v1_i];
    MutNetlistVertex v2 = (*this)[v2_i];

    detail::ASSERT(v1.is_valid(), "Attempted to connect to a null/removed vertex.");
    detail::ASSERT(v2.is_valid(), "Attempted to connect to a null/removed vertex.");

    const NetlistVertexKind k1 = v1.kind();
    const NetlistVertexKind k2 = v2.kind();

    MutNetlistVertex component;
    MutNetlistVertex net;

    if (VertKindIsNet(k1)) {
        detail::ASSERT(!VertKindIsNet(k2), "Attempted to connect two nets.");
        net = v1;
        component = v2;
    } else {
        detail::ASSERT(VertKindIsNet(k2), "Attempted to connect two components. Use a net.");
        net = v2;
        component = v1;
    }

    detail::ASSERT(component.kind() == EdgeKindInfo<KIND>::AssociatedVertKind,
                   "Attempted to connect an edge to a non-matching component.");

    NetlistEdgeIdx e = alloc_edge_slot();
    init_edge(&mEdges, e, KIND, component.idx(), net.idx(),
              std::numeric_limits<double>::signaling_NaN(), {});

    using Data = typename EdgeKindInfo<KIND>::AssociatedData;
    NetlistEdgeValue *val = mEdges.mValue_arr + e.value;
    new (&(val->*EdgeKindInfo<KIND>::AssociatedField)) Data(std::move(data));

    net.edges_mut()->push_back(e);
    component.edges_mut()->push_back(e);

    return e;
}
// Explicitly instantiate
#define EDGE_KIND(NAME, ASSOCIATED_VERT_KIND, UNION_FIELD)                                         \
    template NetlistEdgeIdx Netlist::connect<NetlistEdgeKind::NAME>(                               \
        NetlistVertexIdx, NetlistVertexIdx,                                                        \
        EdgeKindInfo<NetlistEdgeKind::NAME>::AssociatedData &&);
EDGE_KINDS
#undef EDGE_KIND

// =================
// Removal
// =================

// Remove edge e from v's adjacency list, if present.
void Netlist::unlink_edge(NetlistVertexIdx v, NetlistEdgeIdx e) {
    auto &list = *MutNetlistVertex{*this, v}.edges_mut();
    const int64_t n = list.size();
    for (int64_t i = 0; i < n; ++i) {
        if (list[static_cast<size_t>(i)] == e) {
            if (i != n - 1) {
                list[static_cast<size_t>(i)] = list[static_cast<size_t>(n - 1)];
            }
            list.resize(n - 1);
            return;
        }
    }
}

void Netlist::destroy_edge(NetlistEdgeIdx idx) {
    auto e = MutNetlistEdge{*this, idx};
    NetlistVertexIdx a = e.component().idx();
    NetlistVertexIdx b = e.net().idx();

    unlink_edge(a, idx);
    unlink_edge(b, idx);

#define FIELD(T, N) std::destroy_at(mEdges.N + idx.value);
    EDGE_FIELDS
#undef FIELD

    mEdges.mKind_arr[idx.value] = EDGE_DEAD;
    mEdges.mValue_arr[idx.value].freeNext = mEdges.freeHead;
    mEdges.freeHead = idx;
}

void Netlist::remove(NetlistVertexIdx idx) {
    MutNetlistVertex v = {*this, idx};
    // Destroy every incident edge. destroy_edge() unlinks from both endpoints,
    // so this list shrinks as we go.
    while (v.edges_mut()->size() > 0) {
        const int64_t last = v.edges_mut()->size() - 1;
        NetlistEdgeIdx e = (*v.edges_mut())[static_cast<size_t>(last)];
        destroy_edge(e);
    }

#define FIELD(T, N) std::destroy_at(mVertices.N + idx.value);
    VERTEX_FIELDS
#undef FIELD

    mVertices.mKind_arr[idx.value] = VERT_DEAD;
    mVertices.mValue_arr[idx.value].freeNext = mVertices.freeHead;
    mVertices.freeHead = idx;
}
