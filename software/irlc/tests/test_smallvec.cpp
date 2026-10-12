#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>

#include <irl/detail/smallvec.hpp>

namespace {

// Counts live objects so tests can detect leaks / double destruction for
// non-trivial element types.
struct Tracked {
    static int live;
    static int constructed;
    static int destroyed;

    int value = 0;

    Tracked() { ++live, ++constructed; }
    explicit Tracked(int v) : value(v) { ++live, ++constructed; }
    Tracked(const Tracked &other) : value(other.value) { ++live, ++constructed; }
    Tracked(Tracked &&other) noexcept : value(other.value) {
        other.value = -1;
        ++live, ++constructed;
    }
    Tracked &operator=(const Tracked &) = default;
    Tracked &operator=(Tracked &&) = default;
    ~Tracked() { --live, ++destroyed; }
};

int Tracked::live = 0;
int Tracked::constructed = 0;
int Tracked::destroyed = 0;

} // namespace

TEST_CASE("SmallVec starts empty and inline", "[smallvec]") {
    irl::detail::SmallVec<int, 4> v;

    REQUIRE(v.size() == 0);
    REQUIRE(v.capacity() == 4);
    REQUIRE(v.begin() == v.end());
}

TEST_CASE("SmallVec push_back stays inline below capacity", "[smallvec]") {
    irl::detail::SmallVec<int, 4> v;

    for (int i = 0; i < 4; i++) {
        v.push_back(static_cast<int>(i));
    }

    REQUIRE(v.size() == 4);
    REQUIRE(v.capacity() == 4);
    for (int i = 0; i < 4; i++) {
        REQUIRE(v[static_cast<size_t>(i)] == i);
    }
}

TEST_CASE("SmallVec grows to the heap and preserves order", "[smallvec]") {
    irl::detail::SmallVec<int, 4> v;

    for (int i = 0; i < 100; i++) {
        v.push_back(static_cast<int>(i));
    }

    REQUIRE(v.size() == 100);
    REQUIRE(v.capacity() >= 100);
    for (int i = 0; i < 100; i++) {
        REQUIRE(v[static_cast<size_t>(i)] == i);
    }
}

TEST_CASE("SmallVec ensure_capacity handles requests beyond 2x capacity", "[smallvec]") {
    irl::detail::SmallVec<int, 4> v;

    v.ensure_capacity(1000);

    REQUIRE(v.capacity() >= 1000);

    // Still usable after the jump.
    v.push_back(7);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0] == 7);
}

TEST_CASE("SmallVec resize grows with default constructed elements", "[smallvec]") {
    irl::detail::SmallVec<int, 4> v;

    v.resize(10);

    REQUIRE(v.size() == 10);
    REQUIRE(v.capacity() >= 10);
    for (int i = 0; i < 10; i++) {
        REQUIRE(v[static_cast<size_t>(i)] == 0);
    }
}

TEST_CASE("SmallVec resize shrinks and keeps capacity", "[smallvec]") {
    irl::detail::SmallVec<int, 4> v;

    for (int i = 0; i < 10; i++) {
        v.push_back(static_cast<int>(i));
    }
    const auto cap = v.capacity();
    REQUIRE(cap > 4);

    v.resize(3);

    REQUIRE(v.size() == 3);
    REQUIRE(v.capacity() == cap);
    for (int i = 0; i < 3; i++) {
        REQUIRE(v[static_cast<size_t>(i)] == i);
    }
}

TEST_CASE("SmallVec clear empties without losing heap storage", "[smallvec]") {
    irl::detail::SmallVec<int, 4> v;

    for (int i = 0; i < 20; i++) {
        v.push_back(static_cast<int>(i));
    }
    const auto cap = v.capacity();

    v.clear();

    REQUIRE(v.size() == 0);
    // The heap allocation must be retained, not leaked/dropped.
    REQUIRE(v.capacity() == cap);

    v.push_back(42);
    REQUIRE(v.size() == 1);
    REQUIRE(v[0] == 42);
}

TEST_CASE("SmallVec destroys elements exactly once", "[smallvec]") {
    Tracked::live = 0;
    Tracked::constructed = 0;
    Tracked::destroyed = 0;

    {
        irl::detail::SmallVec<Tracked, 4> v;
        for (int i = 0; i < 32; i++) {
            v.push_back(Tracked(i));
        }
        REQUIRE(v.size() == 32);
        for (int i = 0; i < 32; i++) {
            REQUIRE(v[static_cast<size_t>(i)].value == i);
        }
        REQUIRE(Tracked::live == 32);

        v.resize(8);
        REQUIRE(Tracked::live == 8);
    }

    REQUIRE(Tracked::live == 0);
    REQUIRE(Tracked::constructed == Tracked::destroyed);
}

TEST_CASE("SmallVec move constructor moves inline elements", "[smallvec]") {
    irl::detail::SmallVec<std::string, 4> v;
    v.push_back("a");
    v.push_back("b");
    v.push_back("c");

    irl::detail::SmallVec<std::string, 4> moved(std::move(v));

    REQUIRE(moved.size() == 3);
    REQUIRE(moved[0] == "a");
    REQUIRE(moved[1] == "b");
    REQUIRE(moved[2] == "c");
    REQUIRE(v.size() == 0);
}

TEST_CASE("SmallVec move constructor transfers heap storage", "[smallvec]") {
    irl::detail::SmallVec<std::string, 4> v;
    for (int i = 0; i < 16; i++) {
        v.push_back("value" + std::to_string(i));
    }

    irl::detail::SmallVec<std::string, 4> moved(std::move(v));

    REQUIRE(moved.size() == 16);
    for (int i = 0; i < 16; i++) {
        REQUIRE(moved[static_cast<size_t>(i)] == "value" + std::to_string(i));
    }
    REQUIRE(v.size() == 0);
    REQUIRE(v.capacity() == 4);
}
