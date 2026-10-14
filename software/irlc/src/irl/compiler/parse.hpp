#pragma once

#include <array>
#include <memory>
#include <string_view>
#include <vector>

#include "irl/compiler.hpp"

namespace irl {

// An interface for a parser that can take an input circuit into a raw netlist
class INetlistParser {
  protected:
    CompilerOptions const &mOpts;

    INetlistParser(CompilerOptions const &opts) : mOpts(opts) {}

  public:
    virtual ~INetlistParser() = default;
    virtual const char *parser_name() const = 0;

    virtual ParseResult try_parse(std::string_view file_contents,
                                  std::string_view file_name) const = 0;
};

// class ParseException : public std::runtime_error {
//   private:
//     ParseException(std::string what) : std::runtime_error(what) {}
//
//   public:
//     typedef enum parseExceptionKind { LEX_ERROR, PARSE_ERROR } ParseExceptionKind;
//
//     static inline ParseException create(ParseExceptionKind kind, std::string description,
//                                         std::string_view filename, uint32_t line) {
//         std::ostringstream what;
//         if (kind == LEX_ERROR) {
//             what << "Lexing Error";
//         } else if (kind == PARSE_ERROR) {
//             what << "Parsing Error";
//         }
//         what << " at [" << filename << ":" << line << "]: " << description;
//         std::string what_str = what.str();
//         return ParseException(what_str);
//     };
// };

// Simple spice netlist format parser
class SpiceParser : public INetlistParser {
  public:
    SpiceParser(CompilerOptions const &opts) : INetlistParser(opts) {}
    virtual ~SpiceParser() override = default;

    static bool matches_filename(std::string_view filename);

    virtual const char *parser_name() const override { return "Spice Parser"; };

    virtual ParseResult try_parse(std::string_view file_contents,
                                  std::string_view file_name) const override;
};

// KiCad eeschema xml parser
class EeschemaParser : public INetlistParser {
  public:
    EeschemaParser(CompilerOptions const &opts) : INetlistParser(opts) {}
    virtual ~EeschemaParser() override = default;

    static bool matches_filename(std::string_view filename);

    virtual const char *parser_name() const override { return "Eeschema Parser"; };

    virtual ParseResult try_parse(std::string_view file_contents,
                                  std::string_view file_name) const override;
};

// Factory for making parsers
template <typename... Parsers> class ParserFactory {
  public:
    // Gets the parsers in a prioritized list based on the terrible filename heuristic
    // Parser 0 is best, parser 1 second best, etc
    static std::array<std::unique_ptr<INetlistParser>, sizeof...(Parsers)>
    make_parsers_prioritized(std::string_view filename, CompilerOptions const &opts) {
        std::array<std::unique_ptr<INetlistParser>, sizeof...(Parsers)> all{};

        std::unique_ptr<INetlistParser> *good_ind = &all[0];
        std::unique_ptr<INetlistParser> *bad_ind = &all[all.size() - 1];

        (
            [&]() {
                bool parser_good = Parsers::matches_filename(filename);
                if (parser_good) {
                    *good_ind = std::unique_ptr<INetlistParser>(new Parsers(opts));
                    good_ind++;
                } else {
                    *bad_ind = std::unique_ptr<INetlistParser>(new Parsers(opts));
                    bad_ind--;
                }
            }(),
            ...);

        return all;
    }
};

typedef ParserFactory<EeschemaParser, SpiceParser> AllParsersFactory;

}; // namespace irl
