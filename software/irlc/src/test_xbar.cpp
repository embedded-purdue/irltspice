#include <cstdlib>

#include <irl/driver.hpp>

using namespace irl::driver;

int main(int argc, const char **argv) {
    if (argc == 1) {
        return -1;
    }

    IrlDriver driver{argv[1]};
    driver.testSendProg();
}
