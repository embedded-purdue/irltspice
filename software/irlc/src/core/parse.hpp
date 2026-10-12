#pragma once

#include <array>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "core/compiler.hpp"
#include "irl/netlist.hpp"

namespace irl {

// An interface for a parser that can take an input circuit into a raw netlist
class INetlistParser {
  protected:
    IrlCompiler const &compiler;
    INetlistParser(IrlCompiler const &compiler) : compiler(compiler) {}

  public:
    virtual ~INetlistParser() = default;
    virtual const char *parser_name() const = 0;

    virtual std::unique_ptr<Netlist> try_parse(const std::string &filename,
                                               std::string_view in) const = 0;
};

class ParseException : public std::runtime_error {
  private:
    ParseException(std::string what) : std::runtime_error(what) {}

  public:
    typedef enum parseExceptionKind { LEX_ERROR, PARSE_ERROR } ParseExceptionKind;

    static inline ParseException create(ParseExceptionKind kind, std::string description,
                                        std::string_view filename, uint32_t line) {
        std::ostringstream what;
        if (kind == LEX_ERROR) {
            what << "Lexing Error";
        } else if (kind == PARSE_ERROR) {
            what << "Parsing Error";
        }
        what << " at [" << filename << ":" << line << "]: " << description;
        std::string what_str = what.str();
        return ParseException(what_str);
    };
};

// Simple spice netlist format parser
class SpiceParser : public INetlistParser {
  public:
    SpiceParser(IrlCompiler const &compiler) : INetlistParser(compiler) {}
    virtual ~SpiceParser() override = default;

    static bool matches_filename(const std::string &filename);

    virtual const char *parser_name() const override { return "Spice Parser"; };

    virtual std::unique_ptr<Netlist> try_parse(const std::string &filename,
                                               std::string_view in) const override;
};

// KiCad eeschema xml parser
class EeschemaParser : public INetlistParser {
  public:
    EeschemaParser(IrlCompiler const &compiler) : INetlistParser(compiler) {}
    virtual ~EeschemaParser() override = default;

    static bool matches_filename(const std::string &filename);

    virtual const char *parser_name() const override { return "Eeschema Parser"; };

    virtual std::unique_ptr<Netlist> try_parse(const std::string &filename,
                                               std::string_view in) const override;
};

// Factory for making parsers
template <typename... Parsers> class ParserFactory {
  public:
    // Gets the parsers in a prioritized list based on the terrible filename heuristic
    // Parser 0 is best, parser 1 second best, etc
    static std::array<std::unique_ptr<INetlistParser>, sizeof...(Parsers)>
    make_parsers_prioritized(const std::string &filename, IrlCompiler const &compiler) {
        std::array<std::unique_ptr<INetlistParser>, sizeof...(Parsers)> all{};

        std::unique_ptr<INetlistParser> *good_ind = &all[0];
        std::unique_ptr<INetlistParser> *bad_ind = &all[all.size() - 1];

        (
            [&]() {
                bool parser_good = Parsers::matches_filename(filename);
                if (parser_good) {
                    *good_ind = std::unique_ptr<INetlistParser>(new Parsers(compiler));
                    good_ind++;
                } else {
                    *bad_ind = std::unique_ptr<INetlistParser>(new Parsers(compiler));
                    bad_ind--;
                }
            }(),
            ...);

        return all;
    }
};

typedef ParserFactory<EeschemaParser, SpiceParser> AllParsersFactory;

}; // namespace irl
