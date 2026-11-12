// A simple application to send a programming command to a crossbar specified on the command line
// Just links irldriver, does not use any irlcompiler functionality

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

#include <irl/driver.hpp>

#include "irl/netlist.hpp"

using namespace irl;
using namespace irl::driver;

void print_usage(const char *name) {
    printf("Usage: %s SERIAL_PORT MOTHER_ID [DAUGHTER_ID] -- IC_ID ROW COL ON/OFF\n"
           "  where ON/OFF is 1/0\n",
           name);
}

int main(int argc, const char **argv) {
    int dashloc = -1;
    for (int i = 1; i < argc; i++) {
        if (std::string_view{argv[i]} == "--") {
            dashloc = i;
        }
    }
    int pre_dash = dashloc - 1;
    int post_dash = argc - 1 - dashloc;
    if (dashloc < 0 || pre_dash < 2 || pre_dash > 3 || post_dash != 4) {
        print_usage(argv[0]);
        return -1;
    }

    ProgramXbar pxb{
        .xbarId =
            XbarId{
                .ic_id = static_cast<uint16_t>(strtol(argv[dashloc + 1], nullptr, 10)),
                .board_id =
                    BoardId{
                        .mother_id = static_cast<MotherId_t>(strtol(argv[2], nullptr, 10)),
                        .daughter_id = static_cast<MotherId_t>(
                            pre_dash == 3 ? strtol(argv[1], nullptr, 10) : NO_DAUGHTERBOARD),
                    },
            },
        .row = static_cast<uint16_t>(strtol(argv[dashloc + 2], nullptr, 10)),
        .col = static_cast<uint16_t>(strtol(argv[dashloc + 3], nullptr, 10)),
        .connect = static_cast<uint16_t>(strtol(argv[dashloc + 4], nullptr, 10)) > 0,
    };

    std::cout << "Connecting...\n";
    IrlDriver driver{argv[1]};
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
