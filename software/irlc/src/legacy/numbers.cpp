#include "core/numbers.hpp"

#include <charconv>
#include <sstream>
#include <stdexcept>

using namespace std;

val_pico val_pico_t::from_str(std::string_view str) {
    const string_view numbers("1234567890");
    size_t s_i = str.find_first_not_of(numbers);
    if (s_i == string_view::npos) {
        s_i = str.size();
    }
    if (s_i == 0) {
        throw runtime_error(
            (ostringstream() << "Invalid number: " << str << ". Does not start with a number")
                .str());
    }
    string_view num = str.substr(0, s_i);
    string_view suffix = str.substr(s_i);

    if (suffix.size() >= 2) {
        throw runtime_error((ostringstream() << "Invalid number: " << str << ". Suffix: \""
                                             << suffix << "\" is more than one character long")
                                .str());
    }

    uint64_t value;
    std::from_chars_result result = std::from_chars(num.data(), num.data() + num.size(), value);

    if (result.ec != std::errc{}) {
        throw runtime_error((ostringstream() << "Invalid number: " << str
                                             << ". Failed to parse numerical component: " << num)
                                .str());
    }

    if (suffix.size() == 0) {
        return val_pico_t::no_scaling(value);
    }

    switch (suffix[0]) {
    case 'p':
        return operator""_p(value);
        break;
    case 'n':
        return operator""_n(value);
        break;
    case 'u':
        return operator""_u(value);
        break;
    case 'm':
        return operator""_m(value);
        break;
    case 'k':
        return operator""_k(value);
        break;
    case 'M':
        return operator""_M(value);
        break;
    default:
        throw runtime_error(
            (ostringstream() << "Invalid number: " << str << ". Unknown suffix: " << suffix).str());
        break;
    }
}

std::ostream &operator<<(std::ostream &os, val_pico_t const &val) {
    if (val >= 1_M && val % 1_M == 0) {
        os << (val / 1_M).v << "M";
    } else if (val >= 1_k && val % 1_k == 0) {
        os << (val / 1_k).v << "k";
    } else if (val >= val_pico_t::no_scaling(1) && val % val_pico_t::no_scaling(1) == 0) {
        os << (val / val_pico_t::no_scaling(1)).v;
    } else if (val >= 1_m && val % 1_m == 0) {
        os << (val / 1_m).v << "m";
    } else if (val >= 1_u && val % 1_u == 0) {
        os << (val / 1_u).v << "u";
    } else if (val >= 1_n && val % 1_n == 0) {
        os << (val / 1_n).v << "n";
    } else {
        os << val.v << "p";
    }
    return os;
}
