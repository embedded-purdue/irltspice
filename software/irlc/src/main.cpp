#include "core/compiler.hpp"
#include <array>
#include <boost/program_options.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <map>

using std::string;

namespace po = boost::program_options;

template <size_t N> consteval std::array<char, N> flagify(std::array<char, N> long_name) {
    std::array<char, N> out(long_name);
    std::replace(out.begin(), out.end(), '_', '-');
    return out;
}

template <std::size_t... Ns> auto consteval concatenate(const std::array<char, Ns> &...arrays) {
    std::array<char, ((Ns - 1) + ...) + 1> result;
    std::size_t index{};

    ((std::copy_n(arrays.begin(), Ns - 1, result.begin() + index), index += Ns - 1), ...);

    result[result.size() - 1] = '\0';

    return result;
}

IrlCompilerOptions build_compiler_options(std::map<std::string, po::variable_value> &vm);

int main(int argc, char *argv[]) {

    std::map<IrlCompilerOptionGroup, po::options_description> opt_groups{};
    for (auto opt : IRL_COMPILER_OPTION_GROUPS) {
        opt_groups.emplace(opt, po::options_description(ilrCompilerOptionGroupName(opt)));
    }

#define CHAR_ARR(word) std::to_array(word)

    // Actually the coolest pattern I've ever seen
#define FLAG_OPT(LONG_N, SHORT_N, MSG, CATEGORY)                                                   \
    do {                                                                                           \
        opt_groups[CATEGORY].add_options()(                                                        \
            concatenate(flagify(CHAR_ARR(QUOTE(LONG_N))), CHAR_ARR(","), CHAR_ARR(SHORT_N))        \
                .data(),                                                                           \
            MSG);                                                                                  \
    } while (0);
#define TYPED_OPT(LONG_N, SHORT_N, TYPE, DEFAULT, MSG, CATEGORY)                                   \
    do {                                                                                           \
        opt_groups[CATEGORY].add_options()(                                                        \
            concatenate(flagify(CHAR_ARR(QUOTE(LONG_N))), CHAR_ARR(","), CHAR_ARR(SHORT_N))        \
                .data(),                                                                           \
            po::value<TYPE>(), MSG);                                                               \
                                                                                                   \
    } while (0);

    // Like wtf even is this
    COMPILER_OPTIONS

#undef FLAG_OPT
#undef TYPED_OPT

    po::options_description cmdline_desc;

    for (auto &pair : opt_groups) {
        cmdline_desc.add(pair.second);
    }

    po::positional_options_description positional;
    positional.add(flagify(CHAR_ARR(EX_AND_QUOTE(INFILE_OPT))).data(), -1);

    po::variables_map vm;
    po::store(
        po::command_line_parser(argc, argv).options(cmdline_desc).positional(positional).run(), vm);
    po::store(po::parse_command_line(argc, argv, cmdline_desc), vm);
    po::notify(vm);

    IrlCompilerOptions opts = build_compiler_options(vm);

    IrlCompiler compiler(std::move(opts), std::cout);

    if (compiler.opts.help || argc == 1) {
        std::cout << "Usage:\n\n"
                  << "  irlc [OPTIONS] <input-file>\n";
        std::cout << cmdline_desc << std::endl;
        return -1;
    }

    return compiler.invoke();
}

IrlCompilerOptions build_compiler_options(std::map<std::string, po::variable_value> &vm) {

    // #define FLsdfjslkdfjlsdjflks∆lkjdsAG_OPT(LONG_N, SHORT_N, MSG, CATEGORY) \
    //     std::cout << QUOTE(LONG_N) << " - count: " << vm.count(flagify(QUOTE(LONG_N)).c_str()) <<
    //     "\n";
    // #define TYPED_OPT(LONG_N, SHORT_N, TYPE, MSG, CATEGORY) \
    //     std::cout << QUOTE(LONG_N) << " - count: " << vm.count(flagify(QUOTE(LONG_N)).c_str()) <<
    //     "\n"; COMPILER_OPTIONS
    // #undef FLAG_OPT
    // #undef TYPED_OPT

#define FLAG_OPT(LONG_N, SHORT_N, MSG, CATEGORY)                                                   \
    .LONG_N = vm.contains(flagify(CHAR_ARR(QUOTE(LONG_N))).data()),
#define TYPED_OPT(LONG_N, SHORT_N, TYPE, DEFAULT, MSG, CATEGORY)                                   \
    .LONG_N = vm.contains(flagify(CHAR_ARR(QUOTE(LONG_N))).data())                                 \
                  ? vm[flagify(CHAR_ARR(QUOTE(LONG_N))).data()].as<TYPE>()                         \
                  : DEFAULT,

    return {COMPILER_OPTIONS};

#undef FLAG_OPT
#undef TYPED_OPT
}
