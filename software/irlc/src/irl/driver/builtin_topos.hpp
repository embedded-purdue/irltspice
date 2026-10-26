#pragma once

// A collection of builtin topologies.
// These are JSONC (JSON with `//` comments) parsed by the driver's
// topology_json layer. See docs/API.md for the schema.

constexpr auto mother_rev1 = R"(
{
    "class" : "MOTHER",
    "kind" : null, // Not meaningful for mother. Int for daughter.
    "revision" : 0, // Not meaningful (yet) for daughter.
    "topology": {
        // Potentially have something like "templates" up here but idk
        "components": {
            "U1": {
                "kind": "TOPO_XBAR",
                "ic_id": 1,
                "pins": [
                    { "dest": "cir_in", "data": { "side": "ROW", "index": 0 }, "len": 0.0 }, // Absent "kind" implies empty string
                    { "dest": "internal_1", "data": { "side": "ROW", "index": 1 }, "len": 0.0 }, // Random name detected as just a normal NET
                    { "dest": "internal_2", "data": { "side": "ROW", "index": 2 }, "len": 0.0 },
                    { "dest": "GND", "data": { "side": "ROW", "index": 3 }, "len": 0.0 }, // Name "GND" auto detected as a NET_V_GND as in a user's circuit
                    { "dest": "cir_out", "data": { "side": "ROW", "index": 4 }, "len": 0.0 },
                    { "dest": "gma_out", "data": { "side": "ROW", "index": 5 }, "len": 0.0 },
                    { "dest": "cma_in", "data": { "side": "ROW", "index": 6 }, "len": 0.0 },

                    { "dest": "d1_1", "data": { "side": "COL", "index": 0 }, "len": 0.0 },
                    { "dest": "d1_2", "data": { "side": "COL", "index": 1 }, "len": 0.0 },
                    { "dest": "d2_1", "data": { "side": "COL", "index": 2 }, "len": 0.0 },
                    { "dest": "d2_2", "data": { "side": "COL", "index": 3 }, "len": 0.0 },
                    { "dest": "d3_1", "data": { "side": "COL", "index": 4 }, "len": 0.0 },
                    { "dest": "d3_2", "data": { "side": "COL", "index": 5 }, "len": 0.0 },
                    { "dest": "d4_1", "data": { "side": "COL", "index": 6 }, "len": 0.0 },
                    { "dest": "d4_2", "data": { "side": "COL", "index": 7 }, "len": 0.0 }
                ]
            }
        },
        "ports": {
            "from_grandmother1": { "kind": "MM", "pins": [
                { "kind": "CHILD", "dest": "gma_out", "data": { "index": 0 }, "len": 0.0 }
            ] },
            "to_mother1": { "kind": "MM", "pins": [
                { "kind": "PARENT", "dest": "cma_in", "data": { "index": 0 }, "len": 0.0 }
            ] },

            "DAUGHTER1": { "kind": "MD", "daughter_id": 0, "pins": [
                { "kind": "PARENT", "dest": "d1_1", "data": { "index": 0 }, "len": 0.0 },
                { "kind": "PARENT", "dest": "d1_2", "data": { "index": 1 }, "len": 0.0 }
            ] },
            "DAUGHTER2": { "kind": "MD", "daughter_id": 1, "pins": [
                { "kind": "PARENT", "dest": "d2_1", "data": { "index": 0 }, "len": 0.0 },
                { "kind": "PARENT", "dest": "d2_2", "data": { "index": 1 }, "len": 0.0 }
            ] },

            "DAUGHTER3": { "kind": "MD", "daughter_id": 2, "pins": [
                { "kind": "PARENT", "dest": "d3_1", "data": { "index": 0 }, "len": 0.0 },
                { "kind": "PARENT", "dest": "d3_2", "data": { "index": 1 }, "len": 0.0 }
            ] },

            "DAUGHTER4": { "kind": "MD", "daughter_id": 3, "pins": [
                { "kind": "PARENT", "dest": "d4_1", "data": { "index": 0 }, "len": 0.0 },
                { "kind": "PARENT", "dest": "d4_2", "data": { "index": 1 }, "len": 0.0 }
            ] },

            "INPUT_PORT": { "kind": "GENERIC", "pins": [
                { "kind": "PARENT", "dest": "cir_in", "len": 0.0 }
            ] },

            "OUTPUT_PORT": { "kind": "GENERIC", "pins": [
                { "kind": "PARENT", "dest": "cir_out", "len": 0.0 }
            ] }
        }
    }
}
)";
