#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <string_view>

#include "irl/compiler.hpp"
#include "irl/compiler/parse.hpp"

using namespace irl;

TEST_CASE("Factory Identifies Parsers", "[parse]") {

    std::ostringstream dummy_out{};

    std::string filename1 = "fakefile.cir";
    std::string filename2 = "fakefile.xml";

    std::stringstream filecontents;
    filecontents.str("Blah Blah Blah");

    auto spiceparsers = AllParsersFactory::make_parsers_prioritized(filename1, {});

    SpiceParser spice{{}};
    EeschemaParser ee{{}};

    REQUIRE(spiceparsers.size() == 2);
    REQUIRE(spiceparsers[0]->parser_name() == spice.parser_name());
    REQUIRE(spiceparsers[1]->parser_name() == ee.parser_name());

    auto eeschemaparsers = AllParsersFactory::make_parsers_prioritized(filename2, {});

    REQUIRE(eeschemaparsers.size() == 2);
    REQUIRE(eeschemaparsers[0]->parser_name() == ee.parser_name());
    REQUIRE(eeschemaparsers[1]->parser_name() == spice.parser_name());
}

TEST_CASE("Parsing Tokenizes", "[parse]") {
    std::ostringstream dummy_out{};

    std::string filename1 = "../../tests/data/User_BJT.cir";

    std::ifstream file(filename1);

    std::filesystem::path currentPath = std::filesystem::current_path();

    std::cout << currentPath << "\n";

    REQUIRE(file.is_open());

    std::string content((std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>()));

    auto spiceparsers = AllParsersFactory::make_parsers_prioritized(filename1, {});

    spiceparsers[0]->try_parse(content, filename1);

    REQUIRE(true);
}

TEST_CASE("String view hashing", "[misc]") {
    typedef std::unordered_map<std::string_view, int> NetNameMap;

    std::string str("abc 123 abc");

    auto sv1 = std::string_view(str.data(), 3);
    auto sv2 = std::string_view(str.data() + 4, str.data() + 7);
    auto sv3 = std::string_view(str.data() + 8, str.data() + 11);

    auto hash = std::hash<std::string_view>{};
    REQUIRE(hash(sv1) != hash(sv2));
    REQUIRE(hash(sv1) == hash(sv3));

    NetNameMap map{};

    map[sv1] = 1;
    map[sv2] = 2;
    map[sv3] = 3;

    REQUIRE(map[sv1] == 3);
}
