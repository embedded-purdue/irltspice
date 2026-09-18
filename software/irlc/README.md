# The IRLTSPICE Compiler - IRLC
[![Multiple Platform IRLC Build](https://github.com/hughe349/Senior_Design_IRLTSPICE/actions/workflows/build-multi-platform.yml/badge.svg?branch=main)](https://github.com/hughe349/Senior_Design_IRLTSPICE/actions/workflows/build-multi-platform.yml)

The IRLTSPICE Compiler (irlc), synthesizes a user's netlist into a list of programming commands sent to their Transfigurable and Super Programmable Integrated Circuit Element (TSPICE).
It's possible we got carried away with the acronymns.

## Setup
1. Ensure systems packages are installed:
    - Protobuf
1. To build/setup env run:
    ```bash
    mkdir build
    cmake . -B build
    cmake --build build
    ```
1. To test run:
    ```bash
    ctest --test-dir build
    ```

## Dependencies

C++20. Compiles in CI using the following compiler versions:
- Linux (Ubuntu): gcc 13.3.0, clang 18.1.3
- MacOS: AppleClang 17.0.0.17000013
- Windows: MSVC 19.44.35225.0

### Libraries:
- [Boost - 1.90](www.boost.org/)
- [Catch2 - 3.12.0](https://github.com/catchorg/Catch2)
- [Protobuf - 36.1.0](https://protobuf.dev/) - system package

### Tooling:
- cmake >= 3.30 - system package
- [cpm](https://github.com/cpm-cmake/cpm.cmake)@0.42.1 (auto-downloaded by CMake)
