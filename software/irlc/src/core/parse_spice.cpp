#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/debug.hpp"
#include "core/netlist.hpp"
#include "core/parse.hpp"
#include "fast_float/fast_float.h"
#include "irl/detail/util.hpp"
#include "irl/netlist.hpp"

using namespace std;
using namespace irl;
using namespace irl::detail;

bool SpiceParser::matches_filename(const string &filename) {
    return filename.ends_with(".spice") || filename.ends_with(".sp") || filename.ends_with(".cir");
};

// =================================================================================================
// Tokenization
// =================================================================================================

typedef enum tokenKind { END_OF_FILE, NORMAL, NEWLINE, CONTINUATION } TokenKind;

#define IF_NAME(var, name)                                                                         \
    if (var == name)                                                                               \
        return #name;

const char *get_name(TokenKind kind) {
    IF_NAME(kind, END_OF_FILE);
    IF_NAME(kind, NORMAL);
    IF_NAME(kind, NEWLINE);
    IF_NAME(kind, CONTINUATION);
    return "REALLY_REALLY_BAD";
}

// typedef variant<string_view, SpecialToken> Token;

typedef struct token {
    TokenKind kind;
    string_view underlying;
    uint32_t line_number;
} Token;

// Filters out comments and whitespace, leaving just newlines
struct Tokenizer {
    string_view::const_iterator current_begin;
    string_view::const_iterator current_end;
    string_view::const_iterator base_end;

    static inline bool is_newline(const string_view::value_type &val) {
        return val == '\n' || val == '\r';
    }

    static inline bool is_whitespace(const string_view::value_type &val) {
        return val == ' ' || val == '\t';
    }

    bool next_is_first_of_line;
    bool is_continuation;
    uint32_t line_number = 1;

    Tokenizer(string_view in) {
        current_begin = in.begin();
        base_end = in.end();

        // bool next_continuation_allowed = false;
        // bool next_line_commment_allowed = true;
        next_is_first_of_line = false;
        is_continuation = false;

        skip_to_newline();
    }

    // Next deref will give us a newline
    void skip_to_newline() {
        while (!is_newline(*current_begin) && current_begin != base_end) {
            current_begin++;
        }
        // Skip \r\n or just \n or whatever
        optionally_eat('\r');
        assert(*current_begin == '\n');

        current_end = current_begin + 1;
        next_is_first_of_line = true;
        // next_line_commment_allowed = true;
    }

    void optionally_eat(const string_view::value_type &c) {
        if (*current_begin == c) {
            current_begin++;
        }
    }

#define ADVANCE_OR_RET()                                                                           \
    do {                                                                                           \
        current_begin++;                                                                           \
        if (current_begin == base_end) {                                                           \
            return Token{END_OF_FILE};                                                             \
        };                                                                                         \
    } while (0);

    Token next() {
        current_begin = current_end;

        if (current_begin == base_end) {
            return Token{END_OF_FILE, string_view{current_begin, current_end}, line_number};
        }

        if (next_is_first_of_line) {
            line_number += 1;
            while (*current_begin == '*') {
                skip_to_newline();
                ADVANCE_OR_RET();
                line_number += 1;
            }
            if (*current_begin == '+') {
                current_end = current_begin + 1;
                next_is_first_of_line = false;
                return Token{CONTINUATION, string_view{current_begin, current_end}, line_number};
            }
        }
        next_is_first_of_line = false;

        while (is_whitespace(*current_begin)) {
            ADVANCE_OR_RET();
        }

        if (is_newline(*current_begin)) {
            // Just normal end of line
            skip_to_newline();
            return Token{NEWLINE, string_view{current_begin, current_end}, line_number};
        } else if (*current_begin == '$') {
            // Line comment
            skip_to_newline();
            return Token{NEWLINE, string_view{current_begin, current_end}, line_number};
        } else if (*current_begin == ';' && *(current_begin + 1) == ' ') {
            // Line comment
            skip_to_newline();
            return Token{NEWLINE, string_view{current_begin, current_end}, line_number};
        } else if (*current_begin == '/' && *(current_begin + 1) == '/') {
            // Line comment
            skip_to_newline();
            return Token{NEWLINE, string_view{current_begin, current_end}, line_number};
        } else {

            current_end = current_begin;
            // A NORMAL CHARACTER!!!
            while (!is_whitespace(*current_end) && !is_newline(*current_end) &&
                   current_end != base_end) {
                current_end++;
            }
        }

        return Token{NORMAL, string_view{current_begin, current_end}, line_number};
    }
};

// =================================================================================================
// Parse Algorithms
// =================================================================================================

// WARN:
// If you move this out of this file it can't be string views they don't own.
typedef unordered_map<string_view, NetlistVertex *> NetNameMap;
typedef unordered_map<string_view, string_view> ModelMap;

static inline void assert_net_count(int num, string_view reference, const string &filename,
                                    const vector<Token *> &line) {
    if (line.size() != num + 2) {
        throw ParseException::create(ParseException::PARSE_ERROR,
                                     string("Component: ") + string(reference) +
                                         ", connected to bad number of nets. Expected: " +
                                         to_string(num) + ", got: " + to_string(line.size() - 2),
                                     filename, line[0]->line_number);
    }
}

static inline uint32_t parse_custom_id(string_view reference, const string &filename,
                                       const vector<Token *> &line) {
    return -1;
}

static inline double parse_spice_number(string_view reference, const string &filename,
                                        const vector<Token *> &line) {
    Token *val_token = line[line.size() - 1];

    string_view num = val_token->underlying;

    const string_view numbers("0123456789");

    constexpr array<pair<const char *, double>, 11> multipliers{
        pair{"T", 10e12},     pair{"G", 10e9},   pair{"Meg", 10e6}, pair{"K", 10e3},
        pair{"mil", 25.4e-6}, pair{"m", 10e-3},  pair{"u", 10e-6},  pair{"n", 10e-9},
        pair{"p", 10e-12},    pair{"f", 10e-15}, pair{"a", 10e-18}};

    double multiplier = 1;
    double value = 0;
    fast_float::from_chars_result result = fast_float::from_chars(num.begin(), num.end(), value);

    if (result.ec == std::errc{}) {
        string_view suffix{result.ptr, (&num[num.size() - 1]) + 1};
        if (suffix.size() == 0) {
            multiplier = 1;
        } else {
            for (auto const &m : multipliers) {
                if (suffix == m.first) {
                    multiplier = m.second;
                    break;
                }
            }
            if (multiplier == 1) {
                throw ParseException::create(ParseException::PARSE_ERROR,
                                             string("Component: ") + string(reference) +
                                                 " , has bad value. Unrecognizes suffix: \"" +
                                                 string(suffix) + "\"",
                                             filename, val_token->line_number);
            }
        }
    } else {
        throw ParseException::create(ParseException::PARSE_ERROR,
                                     string("Component: ") + string(reference) +
                                         " , has bad value. Couldn't parse number",
                                     filename, val_token->line_number);
    }

    return value;
}

static inline NetlistVertex *get_or_add_net(Netlist &netlist, NetNameMap &netnames,
                                            string_view net_name) {

    if (netnames.contains(net_name)) {
        return netnames[net_name];
    }

    NetlistVertex *net = netlist.new_vertex<NET>(string(net_name), WIRE);

    netnames[net_name] = net;
    return net;
}

static inline NetlistVertex *create_numeric_node(Netlist &netlist, NetlistVertexKind kind,
                                                 std::string &&name, double numeric) {
    switch (kind) {
    case NetlistVertexKind::R:
        return netlist.new_vertex<R>(std::move(name), std::move(numeric));
    case NetlistVertexKind::C:
        return netlist.new_vertex<C>(std::move(name), std::move(numeric));
    default:
        ASSERT(false, "Internal error: non-numeric created with create_numeric_node.");
        return nullptr;
    }
}

static inline NetlistVertex *create_valuelesss_node(Netlist &netlist, NetlistVertexKind kind,
                                                    std::string &&name) {
    switch (kind) {
    case NetlistVertexKind::OPAMP:
        return netlist.new_vertex<OPAMP>(std::move(name), {});
    case NetlistVertexKind::DIODE:
        return netlist.new_vertex<DIODE>(std::move(name), {});
    case NetlistVertexKind::Q_PNP:
        return netlist.new_vertex<Q_PNP>(std::move(name), {});
    case NetlistVertexKind::Q_NPN:
        return netlist.new_vertex<Q_NPN>(std::move(name), {});
    default:
        ASSERT(false, "Internal error: non-valueless created with create_valuelesss_node");
        return nullptr;
    }
}

static void connect_valueless(Netlist &netlist, NetlistVertex *v1, NetlistVertex *v2,
                              NetlistEdgeKind kind) {

#define KIND_xbarPinId(NAME, ASSOCIATED_VERT_KIND)
#define KIND_pinIndex(NAME, ASSOCIATED_VERT_KIND)
#define KIND_empty(NAME, ASSOCIATED_VERT_KIND)                                                     \
    case NetlistEdgeKind::NAME:                                                                    \
        netlist.connect<NetlistEdgeKind::NAME>(v1, v2, {});                                        \
        break;
#define EDGE_KIND(NAME, ASSOCIATED_VERT_KIND, UNION_FIELD)                                         \
    KIND_##UNION_FIELD(NAME, ASSOCIATED_VERT_KIND)

    switch (kind) {
        EDGE_KINDS
    default:
        ASSERT(false, "Internal error: non-valueless created with create_valuelesss_node");
    }

#undef EDGE_KIND
}

void add_element(Netlist &netlist, NetNameMap &netnames, ModelMap &models,
                 const vector<Token *> &line, const string &filename) {
    if (line.size() < 2) {
        throw ParseException::create(
            ParseException::PARSE_ERROR,
            "Malformed component line. Must have at least a reference designator and a value",
            filename, line[0]->line_number);
    }

    string_view reference = line[0]->underlying;
    auto first_num = &reference[reference.find_first_of("0123456789")];

    string_view reference_kind = string_view{&*reference.begin(), first_num};
    int reference_num;
    // Parse reference_num
    const char *end_ptr = reference.data() + reference.size();
    auto result = fast_float::from_chars(first_num, end_ptr, reference_num);
    if (reference_kind.length() == 0 || result.ec != std::errc{} || result.ptr != end_ptr ||
        first_num == end_ptr) {
        throw ParseException::create(ParseException::PARSE_ERROR,
                                     "Malformed reference designator: " + string(reference) +
                                         ". Expected to be of form TYPE1234",
                                     filename, line[0]->line_number);
    }

    string_view value = line[line.size() - 1]->underlying;
    if (models.count(value) > 0) {
        value = models[value];
    }

    // NOTE:
    // If slow, we could do a regex/FA on a concatenated reference + value to find proper
    // initializer
    // This is a performative comment to show I thought of a cool idea to do this but was too lazy

    bool found_match = false;
    bool any_ref_found = false;

    for (const auto &recognized : RECOGNIZED_COMPONENTS) {
        bool matches_ref = false;
        for (const auto &ref : recognized.spice_prefixes) {
            if (reference_kind == ref) {
                matches_ref = true;
                any_ref_found = true;
                break;
            }
        }
        if (!matches_ref) {
            continue; // Go to next component
        }
        // Default to match if spice_values has no entries
        bool matches_val = recognized.spice_values.size() == 0;
        // this loop only gets run if spice_values has entries
        for (const auto &val : recognized.spice_values) {
            if (val == value) {
                matches_val = true;
                continue;
            }
        }
        if (!matches_val) {
            continue;
        }

        found_match = true;

        assert_net_count(recognized.pins.size(), reference, filename, line);
        NetlistVertex *vert;
        if (recognized.component_kind == CUSTOM) {
            uint32_t customid = parse_custom_id(reference, filename, line);
            vert = netlist.new_vertex<CUSTOM>(string(reference), std::move(customid));
        }
        if (recognized.numeric_value) {
            double val = parse_spice_number(reference, filename, line);
            vert = create_numeric_node(netlist, recognized.component_kind, string(reference), val);
        } else {
            vert = create_valuelesss_node(netlist, recognized.component_kind, string(reference));
        }

        vert->reserve_pins(recognized.pins.size());
        for (int i = 0; i < recognized.pins.size(); i++) {
            auto pad = get_or_add_net(netlist, netnames, line[1 + i]->underlying);
            connect_valueless(netlist, vert, pad, recognized.pins[i]);
        }

        // We found a match, break out of the loop
        break;
    }

    if (!found_match) {
        if (!any_ref_found)
            throw ParseException::create(ParseException::PARSE_ERROR,
                                         "Unknown reference designator kind/letter used: " +
                                             string(reference),
                                         filename, line[0]->line_number);
        else
            throw ParseException::create(
                ParseException::PARSE_ERROR,
                string("Unknown component value : ") + string(value) +
                    ", used for reference designator: " + string(reference),
                filename, line[0]->line_number);
    }
}

unique_ptr<Netlist> SpiceParser::try_parse(const string &filename, string_view in) const {

    unique_ptr<Netlist> netlist(new Netlist);

    Tokenizer tokenizer(in);

    vector<Token> tokens{};
    Token token;
    do {
        token = tokenizer.next();
        tokens.push_back(token);
    } while (token.kind != END_OF_FILE);

    if (compiler.opts.should_verbose_lex()) {
        for (Token &tok : tokens) {
            compiler.log_fd << "Kind: " << tok.kind << ", Line: " << tok.line_number << ", Val: ["
                            << tok.underlying << "]\n";
        }
    }

    NetNameMap netnames{};
    ModelMap models{};

    Token *tok = &tokens[0];
    vector<Token *> line{};
    while (tok->kind != END_OF_FILE) {
        line.resize(0);

        while (tok->kind == NEWLINE) {
            tok++;
        }
        if (tok->kind == END_OF_FILE)
            break;

        if (tok->kind != NORMAL) {
            throw ParseException::create(ParseException::PARSE_ERROR,
                                         string("Unexpected token: ") + get_name(tok->kind) +
                                             ", Expected: NORMAL",
                                         filename, tok->line_number);
        }

        bool should_continue;
        do {
            should_continue = false;
            while (tok->kind == NORMAL) {
                line.push_back(tok++);
            }
            if (tok->kind == NEWLINE && (tok + 1)->kind == CONTINUATION) {
                tok = tok + 2;
                should_continue = true;
            }
        } while (should_continue);

        if (compiler.opts.should_verbose_parse()) {
            compiler.log_fd << "Line contains: [ ";
            for (const Token *t : line) {
                compiler.log_fd << '\'' << t->underlying << "', ";
            }
            compiler.log_fd << " ]\n";
        }

        if (line[0]->underlying[0] == '.') {
            string_view cmd = string_view(&line[0]->underlying[1], line[0]->underlying.size() - 1);
            if (compiler.opts.should_verbose_parse())
                compiler.log_fd << "Line is a dot cmd: " << cmd << "\n";
            if (cmd == "model" && line.size() == 3) {
                if (compiler.opts.should_verbose_parse())
                    compiler.log_fd << "    Model command!\n";
                models[line[1]->underlying] = line[2]->underlying;
            } else {
                if (compiler.opts.should_verbose_parse())
                    compiler.log_fd << "    Unknown command. Skipping.\n";
            }
        } else {
            add_element(*netlist, netnames, models, line, filename);
            if (compiler.opts.should_verbose_parse())
                compiler.log_fd << " Added!\n";
        }
    }

    if (compiler.opts.should_verbose_parse())
        debug_print_netlist(compiler.log_fd, *netlist);

    return netlist;
}
