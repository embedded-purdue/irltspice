// A simple application to send a programming command to a crossbar specified on the command line
// Just links irldriver, does not use any irlcompiler functionality

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <irl/driver.hpp>

using namespace irl;
using namespace irl::driver;

void print_usage(const char *name) {
    printf(
        "Usage: %s SERIAL_PORT -m MOTHER_ID [-d DAUGHTER_ID] [-t TIMEOUT_MS] IC_ID ROW COL ON/OFF\n"
        "  where ON/OFF is 1/0\n",
        name);
}

std::optional<std::string_view> try_pop_flag(std::string_view flag,
                                             std::vector<std::string_view> &args) {
    for (auto iter = args.begin(); iter != args.end(); iter++) {
        if (*iter == flag) {
            auto next = iter;
            next++;
            if (next == args.end()) {
                break;
            }
            std::string_view out = *next;
            next++;
            args.erase(iter, next);
            return out;
        }
    }
    return std::nullopt;
}

std::string_view pop_flag(std::string_view flag, std::vector<std::string_view> &args) {
    auto out = try_pop_flag(flag, args);
    if (!out) {
        std::cout << "Couldn't find flag: " << flag << std::endl;
        std::exit(-1);
    }
    return *out;
}

int main(int argc, const char **argv) {

    std::vector<std::string_view> args;
    for (int i = 1; i < argc; i++) {
        args.push_back(argv[i]);
    }

    MotherId_t mother_id = strtol(pop_flag("-m", args).data(), nullptr, 10);
    auto daughter_id_o = try_pop_flag("-d", args);
    DaughterId_t daughter_id =
        daughter_id_o ? strtol((*daughter_id_o).data(), nullptr, 10) : NO_DAUGHTERBOARD;
    size_t timeout = strtol(try_pop_flag("-t", args).value_or("100").data(), nullptr, 10);

    ProgramXbar pxb{
        .xbarId =
            XbarId{
                .ic_id = static_cast<uint16_t>(strtol(args[1].data(), nullptr, 10)),
                .board_id = BoardId{.mother_id = mother_id, .daughter_id = daughter_id},
            },
        .row = static_cast<uint16_t>(strtol(args[2].data(), nullptr, 10)),
        .col = static_cast<uint16_t>(strtol(args[3].data(), nullptr, 10)),
        .connect = static_cast<uint16_t>(strtol(args[4].data(), nullptr, 10)) > 0,
    };

    std::cout << "Connecting...\n";
    IrlDriver driver{args[0], {.rx_timeout{timeout}}};
    std::cout << "Connected: " << driver.connected() << "\n";

    bool retry = false;
    bool good = false;
    do {
        auto topo_r = driver.queryTopology();
        if (topo_r.isError()) {
            std::cout << "Failed to get topo. Error: " << *(topo_r.getErr()) << std::endl;
        } else {
            std::cout << "Got topo!" << std::endl;
        }
        auto topo = topo_r.getData();

        auto prog_r = driver.doProgramming(topo, std::span{&pxb, 1});
        if (prog_r.isError()) {
            switch (prog_r.getErr()->code) {
            case CommandErrorCode::STALE_TOPOLOGY:
                std::cout << "Topo changed. Retrying.\n";
                retry = true;
                break;
            default:
                std::cout << "Failed to program: " << *prog_r.getErr();
                break;
            }
        } else {
            good = true;
        }
    } while (retry);

    if (good) {
        std::cout << "Successfully programmed!\n";
        return 0;
    } else {
        std::cout << "Failed to program.";
        return -1;
    }
}
