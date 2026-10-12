
# IRLTSPICE API

We are currently in the middle of a refactor.
This document is our north star, what we want out of the refactor.

This document outlines the basic decisions made for structuring the API.

The API is split into 3 main components:
1. The irltspice driver (`irld`) - knows about hardware-specific details and manages communication.
Is slightly stateful to maintain oscilloscope readings and current topology information.
Intended to run on another thread.
2. The irltspice compiler (`irlc`) - Collection of logic to perform parsing, verification, and routing.
Completely stateless, takes in user designs (either programatically or from files) and topology info from the driver.
3. The irltspice netlist representation (`irlnetlist`) - A common netlist library for user designs and board topology.
Used to exchange information between the driver and the compiler.

Due to the need for `irlc` and `irld` to share binary C++ datastructures (the graph from `irlnetlist`), these three
APIs are always bundled into a single binary (`libirl.a/.so`), although the headers `irl_driver.hpp` and `irl_graph.hpp` can be used independently.

Python consumers can access these three at `irl.driver`, `irl.compiler`, and `irl.netlist`.

