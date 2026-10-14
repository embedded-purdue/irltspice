#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <iterator>
#include <new>
#include <vector>

#include <irl/detail/chunkedlist.hpp>

namespace {

constexpr int DEAD = 0xdead;

// A deliberately nontrivial element so we can observe construct/destruct.
struct Node {
    int kind;
    void *next;
    int payload;

    static inline int live = 0;

    explicit Node(int payload) : kind(1), next(nullptr), payload(payload) { ++live; }
    ~Node() { --live; }
};

} // namespace

namespace irl::detail {
template <> class Chunkable<Node> {
    template <typename, std::size_t, typename> friend class ChunkedList;

    static bool is_dead(const Node &n) { return n.kind == DEAD; }
    static void mark_dead(Node &n) { n.kind = DEAD; }
    static Node *get_freelist(const Node &n) { return static_cast<Node *>(n.next); }
    static void set_freelist(Node &n, Node *p) { n.next = p; }
};
} // namespace irl::detail

using irl::detail::ChunkedList;

namespace {

Node *emplace(ChunkedList<Node, 4> &list, int payload) {
    return new (list.allocate()) Node(payload);
}

} // namespace

TEST_CASE("ChunkedList keeps pointers stable across chunk growth") {
    Node::live = 0;

    ChunkedList<Node, 4> list;
    std::vector<Node *> nodes;
    for (int i = 0; i < 50; ++i) {
        nodes.push_back(emplace(list, i));
    }

    // Many chunks were added (50 > 4), so this also proves earlier slots did
    // not move when later chunks were allocated.
    for (int i = 0; i < 50; ++i) {
        REQUIRE(nodes[i]->payload == i);
    }
    REQUIRE(Node::live == 50);
}

TEST_CASE("ChunkedList frees and reuses slots LIFO") {
    Node::live = 0;

    ChunkedList<Node, 4> list;
    Node *a = emplace(list, 1);
    Node *b = emplace(list, 2);
    Node *c = emplace(list, 3);

    list.deallocate(b);
    REQUIRE(Node::live == 2);

    Node *reused = emplace(list, 42);
    REQUIRE(reused == b);
    REQUIRE(reused->payload == 42);
    REQUIRE(Node::live == 3);

    list.deallocate(a);
    list.deallocate(reused);
    list.deallocate(c);
}

TEST_CASE("ChunkedList iterates live elements only") {
    Node::live = 0;

    ChunkedList<Node, 4> list;
    Node *a = emplace(list, 10);
    Node *b = emplace(list, 20);
    Node *c = emplace(list, 30);
    Node *d = emplace(list, 40);
    Node *e = emplace(list, 50);

    list.deallocate(b);
    list.deallocate(d);

    std::vector<int> seen;
    for (Node &n : list) {
        seen.push_back(n.payload);
    }
    REQUIRE(seen == std::vector<int>{10, 30, 50});
    REQUIRE(std::distance(list.begin(), list.end()) == 3);

    const ChunkedList<Node, 4> &clist = list;
    std::vector<int> cseen;
    for (const Node &n : clist) {
        cseen.push_back(n.payload);
    }
    REQUIRE(cseen == seen);

    ChunkedList<Node, 4>::ConstIter converted = list.begin();
    REQUIRE(converted->payload == 10);

    REQUIRE(a != nullptr);
    REQUIRE(c != nullptr);
    REQUIRE(e != nullptr);
}

TEST_CASE("ChunkedList empty iteration") {
    Node::live = 0;

    ChunkedList<Node, 4> list;
    REQUIRE(list.begin() == list.end());

    std::vector<int> seen;
    for (Node &n : list) {
        seen.push_back(n.payload);
    }
    REQUIRE(seen.empty());
}

TEST_CASE("ChunkedList destroys live elements on teardown") {
    Node::live = 0;
    {
        ChunkedList<Node, 4> list;
        for (int i = 0; i < 10; ++i) {
            emplace(list, i);
        }
        REQUIRE(Node::live == 10);
    }
    REQUIRE(Node::live == 0);
}
