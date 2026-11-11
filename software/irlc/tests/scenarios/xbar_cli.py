"""Scenario: exercise ``examples/xbar_cli.cpp`` against a scripted board.

The crossbar CLI drives the board in three phases::

    queryTopology()   -> MotherCommand{ topo }
    doProgramming()   -> MotherCommand{ reset }               (resetTspice)
                         MotherCommand{ prog: ProgramXbar }   (the one xbar)
                         MotherCommand{ stop }                (endProgramming)
    ... and it expects  -> MotherResponse{ stop: { complete_count: 1 } }

The topology response has all four daughter slots empty, because the driver's
builtin topology database currently only knows mother rev 0. Once daughter
templates are registered, pass e.g.
``daughters=[("basic_daughter", 0xDA0), None, None, None]`` to ``topo_response``
so the driver can unpack a BASIC_V1 board in slot 0.
"""

import firmware_sim as sim


def is_xbar_to_mother(cmd):
    """The xbar command must target a daughterboard, not the motherboard.

    The wire message only carries the daughter *index*; the daughter's *kind*
    (BASIC_V1 is the "normal" one) lives in the topology response, so the kind
    itself is checked there.
    """
    prog = cmd.prog
    assert prog.payload.WhichOneof("data") == "xbar", "expected a ProgramXbar payload"
    assert (
        prog.daughterboard_id == sim.NO_DAUGHTERBOARD
    ), f"xbar addressed to the motherboard (daughter_id={prog.daughterboard_id})"
    return True


STEPS = [
    # queryTopology(): hand back our scripted board.
    ("topo", None, sim.topo_response()),
    # doProgramming() starts by resetting the board; no response expected.
    ("reset", None, None),
    # The single ProgramXbar the CLI asked for.
    ("prog", is_xbar_to_mother, None),
    # endProgramming(): report one command completed.
    ("stop", None, sim.stop_response(1)),
]
