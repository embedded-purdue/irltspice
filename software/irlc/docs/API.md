
# IRLTSPICE API

We are currently in the middle of a refactor.
This document is our north star, what we want out of the refactor.

This document outlines the basic decisions made for structuring the API.

The API is split into 3 main components:
1. The irltspice driver (`irld`) - knows about hardware-specific details and manages communication.
Is slightly stateful to maintain oscilloscope readings and current topology information.
Intended to run on another thread.
2. The irltspice compiler (`irlc`) - Collection of logic to perform parsing, verification, and routing.
Completely stateless, takes in user designs (either programmatically or from files) and topology info from the driver.
3. The irltspice netlist representation (`irlnetlist`) - A common netlist library for user designs and board topology.
Used to exchange information between the driver and the compiler.

Due to the need for `irlc` and `irld` to share binary C++ datastructures (the graph from `irlnetlist`), these three
APIs are always bundled into a single binary (`libirl.a/.so`), although the headers `irl/driver.hpp` and `irl/compiler.hpp` can be used independently.

Python consumers can access these three at `irl.driver`, `irl.compiler`, and `irl.netlist`.

## Netlists and board topology (`irl.netlist`)

The netlist is a bipartite graph. One side holds **net** vertices, the other holds
**component** and **port** vertices. Every edge connects a net to a component or port and records
which pin of that component/port it belongs to. The vertex/edge kinds are enumerated by the
`VERT_KINDS` / `EDGE_KINDS` x-macros in `irl/netlist.hpp`; each kind maps to one field of
`NetlistVertexValue` / `NetlistEdgeValue`.

### Net kinds

- `NET` — a generic net. Any design net may map to any topology net.
- `NET_V_GND`, `NET_V_HIGH`, `NET_V_NEG` — the three fixed board rails (GND, +5V, -5V). Any
  constant voltage in a design may map to a topology rail of the matching kind; the verifier uses
  the kinds to prevent short circuits.
- `NET_SPECIAL` — the escape hatch for signals the compiler cannot know about (circuit inputs and
  outputs, custom components). In a user design these must be prefixed `IRLS_` (for example
  `IRLS_CIRCUIT_INPUT`). A design `NET_SPECIAL` maps to a topology `NET_SPECIAL` **iff their names
  match**, which is also what ties user designs to `PORT_GENERIC` connection points.

### Ports

A port electrically bridges two nets except that it leaves the PCB. Each port has an **N-to-N**
bundle of pins with two sides: **parent** (the board the port lives on) and **child** (the other
side). Pins on the two sides are matched by `pinIndex`. There is exactly one port vertex after
stitching.

- `PORT_GENERIC` — a user-visible 1-to-1 connection point on a mother or daughter. The child side
  is connected to a user-chosen net (usually an `IRLS_`-prefixed special net) via an interface such
  as `--port-connection={"mother":0,"daughter":1,"port":"PORT_NAME1","net":"IRLS_CIRCUIT_INPUT"}`.
  This allows the user to connect arbitrary nets to a PORT_GENERIC only. Note the IRLS_ prefix; if 
  that predix is not specified, it will be assumed that this is a normal net, i.e. two flags 
  representing a jumper wire bridging two boards. If no flag is specified it is assumed the port is 
  disconnected, and nets connected to parent side may be used as normal design nets.
- `PORT_MD` — an N-to-N bundle between a mother and a daughter. A mother may declare multiple (each
  with a distinct `daughter_id`); a daughter declares at most one. When stitching, the daughter
  topology is selected by its `DaughterboardKind` and its single `PORT_MD` is joined with the
  mother's `PORT_MD` of matching `daughter_id`; the mother's `mother_id` is filled in from the
  mother's index in the topology array. Parent/child pins are matched by `pinIndex`.
- `PORT_MM` — an N-to-N bundle chaining mothers. A mother may declare at most two (parent and child
  side). The parent-side `PORT_MM` of motherboard `i` is joined with the child-side `PORT_MM` of
  motherboard `i + 1` in the topology's `motherboards` array.

### Topology templates (JSON)

A board topology is authored as a JSON document extended with `//` comments, with `"components"`
and `"ports"` sections. Built-in mother/daughter topologies live in
`src/irl/driver/builtin_topos.hpp`. Each component/port pin names the net it attaches to via
`"dest"`, carries the edge's kind-specific `"data"` (for example `"side"`/`"index"` for a crossbar
row/column), and gives the pin's trace length in `"len"`.

### Naming and scoping

Local nets are uniquified at instantiation time by prefixing them with `M{m}D{d}:`. This prefixing
**does not** apply to `NET_SPECIAL` names or to nets explicitly connected to a `PORT_GENERIC`, since
those are global by definition.

### Trace length

`NetlistEdge::mTraceLength` is a per-pin length (in mm) from that pin to its net's star point and is
only meaningful for topology netlists (`NaN` means unmodeled). Nets are assumed to be star-shaped
junctions, so the physical distance between two pins is the sum of their stub lengths; nets that
branch at more than one junction are not represented accurately.

