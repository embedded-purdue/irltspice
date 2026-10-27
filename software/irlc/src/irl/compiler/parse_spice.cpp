#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <irl/compiler.hpp>
#include <irl/detail/util.hpp>
#include <irl/netlist.hpp>

#include "fast_float/fast_float.h"
#include "irl/compiler/parse.hpp"
#include "irl/compiler/parse_info.hpp"
#include "irl/core/debug.hpp"

using namespace std;
using namespace irl;
using namespace irl::detail;

bool SpiceParser::matches_filename(std::string_view filename) {
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
typedef unordered_map<string_view, NetlistVertexIdx> NetNameMap;
typedef unordered_map<string_view, string_view> ModelMap;

static inline void add_err(vector<ParseError> &errs, ParseError &&e) {
    errs.emplace_back(std::move(e));
}

static inline bool net_count_good(vector<ParseError> errs, int num, string_view reference,
                                  string_view file_name, const vector<Token *> &line) {
    if (line.size() != num + 2) {
        add_err(errs,
                ParseError{
                    .message = string("Component: ") + string(reference) +
                               ", connected to bad number of nets. Expected: " + to_string(num) +
                               ", got: " + to_string(line.size() - 2),
                    .problematic_region{(const char *)line[0]->underlying.end(),
                                        (const char *)line[line.size() - 1]->underlying.begin()},
                    .file_name = file_name,
                    .lines_begin = line[0]->line_number,
                    .lines_end = line[line.size() - 1]->line_number,
                });
        return false;
    }
    return true;
}

static inline optional<double> parse_spice_number(vector<ParseError> &errs, string_view reference,
                                                  string_view filename,
                                                  const vector<Token *> &line) {
    Token *val_token = line[line.size() - 1];

    string_view num = val_token->underlying;

    auto result = NumericParseCommon(num);

    if (!result.number.has_value()) {
        if (result.problematic_suffix.size() == 0) {
            auto suffix = result.problematic_suffix;
            add_err(errs, ParseError{
                              .message = string("Component: ") + string(reference) +
                                         " , has bad value. Unrecognizes suffix: \"" +
                                         string(suffix) + "\"",
                              .problematic_region = suffix,
                              .file_name = filename,
                              .lines_begin = val_token->line_number,
                              .lines_end = val_token->line_number,
                          });
        } else {
            add_err(errs, ParseError{
                              .message = string("Component: ") + string(reference) +
                                         " , has bad value. Couldn't parse number",
                              .problematic_region = val_token->underlying,
                              .file_name = filename,
                              .lines_begin = val_token->line_number,
                              .lines_end = val_token->line_number,
                          });
        }
    }

    return result.number;
}

template <NetlistVertexKind K>
static inline NetlistVertexIdx add_empty(Netlist &netlist, string_view name) {
    return netlist.new_vertex<K>(string(name), {});
}

static inline NetlistVertexIdx get_or_add_net(Netlist &netlist, string_view net_name) {
    NetlistVertexIdx (*make_net)(Netlist &, string_view);
    NetlistVertexKind kind = get_net_kind(net_name);
    string_view name = get_net_canonical_name(kind, net_name);

    auto net_o = netlist.get_vertex(name);
    if (net_o.has_value()) {
        return *net_o;
    }

    NetlistVertexIdx net = netlist.new_net(name, kind);
    return net;
}

static inline NetlistVertexIdx create_numeric_node(Netlist &netlist, NetlistVertexKind kind,
                                                   std::string &&name, double numeric) {
    switch (kind) {
    case NetlistVertexKind::R:
        return netlist.new_vertex<R>(std::move(name), std::move(numeric));
    case NetlistVertexKind::C:
        return netlist.new_vertex<C>(std::move(name), std::move(numeric));
    default:
        IRL_ASSERT(false, "Internal error: non-numeric created with create_numeric_node.");
        return {};
    }
}

static inline NetlistVertexIdx create_valuelesss_node(Netlist &netlist, NetlistVertexKind kind,
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
        IRL_ASSERT(false, "Internal error: non-valueless created with create_valuelesss_node");
        return {};
    }
}

static void connect_valueless(Netlist &netlist, NetlistVertexIdx v1, NetlistVertexIdx v2,
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
        IRL_ASSERT(false, "Internal error: non-valueless created with create_valuelesss_node");
    }

#undef EDGE_KIND
}

void add_element(Netlist &netlist, vector<ParseError> &errs, ModelMap &models,
                 const vector<Token *> &line, string_view filename) {
    if (line.size() < 2) {
        add_err(errs, ParseError{.message = "Malformed component line. Must have at least a "
                                            "reference designator and a value",
                                 .problematic_region = line[0]->underlying,
                                 .file_name = filename,
                                 .lines_begin = line[0]->line_number,
                                 .lines_end = line[0]->line_number});
        return;
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
        add_err(errs, ParseError{.message = "Malformed reference designator: " + string(reference) +
                                            ". Expected to be of form TYPE1234",
                                 .problematic_region = reference,
                                 .file_name = filename,
                                 .lines_begin = line[0]->line_number,
                                 .lines_end = line[0]->line_number});
        return;
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

        if (!net_count_good(errs, recognized.pins.size(), reference, filename, line)) {
            return;
        }

        NetlistVertexIdx vert;
        if (recognized.numeric_value) {
            optional<double> val = parse_spice_number(errs, reference, filename, line);
            if (!val.has_value()) {
                return;
            }
            vert = create_numeric_node(netlist, recognized.component_kind, string(reference), *val);
        } else {
            vert = create_valuelesss_node(netlist, recognized.component_kind, string(reference));
        }

        netlist[vert].reserve_pins(recognized.pins.size());
        for (int i = 0; i < recognized.pins.size(); i++) {
            auto pad = get_or_add_net(netlist, line[1 + i]->underlying);
            connect_valueless(netlist, vert, pad, recognized.pins[i]);
        }

        // We found a match, break out of the loop
        break;
    }

    if (!found_match) {
        if (!any_ref_found)
            add_err(errs, ParseError{
                              .message = "Unknown reference designator kind/letter used: " +
                                         string(reference),
                              .problematic_region = line[0]->underlying,
                              .file_name = filename,
                              .lines_begin = line[0]->line_number,
                              .lines_end = line[0]->line_number,
                          });
        else
            add_err(errs, ParseError{
                              .message = string("Unknown component value : ") + string(value) +
                                         ", used for reference designator: " + string(reference),
                              .problematic_region = value,
                              .file_name = filename,
                              .lines_begin = line[line.size() - 1]->line_number,
                              .lines_end = line[line.size() - 1]->line_number,
                          });
    }
}

ParseResult SpiceParser::try_parse(std::string_view file_contents,
                                   std::string_view file_name) const {

    vector<ParseError> errors;
    unique_ptr<Netlist> netlist(new Netlist);

    Tokenizer tokenizer(file_contents);

    vector<Token> tokens{};
    Token token;
    do {
        token = tokenizer.next();
        tokens.push_back(token);
    } while (token.kind != END_OF_FILE);

    if (mOpts.should_verbose_lex) {
        for (Token &tok : tokens) {
            mOpts.log_fd << "Kind: " << tok.kind << ", Line: " << tok.line_number << ", Val: ["
                         << tok.underlying << "]\n";
        }
    }

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

        // If we get a bad token, just skip it
        if (tok->kind != NORMAL) {
            add_err(errors,
                    ParseError{.message = string("Unexpected token: ") + get_name(tok->kind) +
                                          ", Expected: NORMAL. Skipping...",
                               .problematic_region = tok->underlying,
                               .file_name = file_name,
                               .lines_begin = tok->line_number,
                               .lines_end = tok->line_number});
            tok++;
            continue;
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

        if (mOpts.should_verbose_parse) {
            mOpts.log_fd << "Line contains: [ ";
            for (const Token *t : line) {
                mOpts.log_fd << '\'' << t->underlying << "', ";
            }
            mOpts.log_fd << " ]\n";
        }

        if (line[0]->underlying[0] == '.') {
            string_view cmd = string_view(&line[0]->underlying[1], line[0]->underlying.size() - 1);
            if (mOpts.should_verbose_parse)
                mOpts.log_fd << "Line is a dot cmd: " << cmd << "\n";
            if (cmd == "model" && line.size() == 3) {
                if (mOpts.should_verbose_parse)
                    mOpts.log_fd << "    Model command!\n";
                models[line[1]->underlying] = line[2]->underlying;
            } else {
                if (mOpts.should_verbose_parse)
                    mOpts.log_fd << "    Unknown command. Skipping.\n";
            }
        } else {
            size_t error_len = errors.size();
            add_element(*netlist, errors, models, line, file_name);
            if (mOpts.should_verbose_parse) {
                if (error_len == errors.size()) {
                    mOpts.log_fd << " Added!\n";
                } else {
                    mOpts.log_fd << " ERRORS WHILE PARSING!!!\n    ";
                    for (int i = error_len; i < errors.size(); i++) {
                        mOpts.log_fd << errors[i].message;
                    }
                    mOpts.log_fd << "\n\n";
                }
            }
        }
    }

    if (mOpts.should_verbose_parse)
        debug_print_netlist(mOpts.log_fd, *netlist);

    return ParseResult{
        .netlist{std::move(netlist)},
        .failures{std::move(errors)},
        .detectedKind = NetlistFileKind::SPICE,
    };
}
