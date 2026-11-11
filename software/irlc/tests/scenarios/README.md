# Firmware simulation scenarios


#  ⚠️   DISCLAIMER  ⚠️   EVERYTHING HERE IS SOOOOOOO CHATTED I LOVE VIBE CODING TESTS

Each file here is a *scenario* for `tests/firmware_sim.py`: it models the
firmware's side of the serial link so a real host program can be tested without
a board. A scenario just exposes a `STEPS` list of
`(request, validate, response)` tuples; see the module docstring in
`../firmware_sim.py` for the full contract.

## Setup

```sh
python3 -m venv .venv && . .venv/bin/activate
pip install -r ../requirements.txt   # just `protobuf`; `protoc` must be on PATH
```

`firmware_sim.py` runs `protoc` itself and caches the generated bindings under
`tests/.cache/pbgen/`, so there is no separate codegen step.

## Running `xbar_cli`

Create a PTY pair; `socat` prints the two slave paths:

```sh
socat -d -d pty,raw,echo=0 pty,raw,echo=0
# -> PTY is /dev/ttysA
# -> PTY is /dev/ttysB
```

Attach the simulated firmware to one end, and the program under test to the
other:

```sh
python3 tests/firmware_sim.py --tty /dev/ttysA tests/scenarios/xbar_cli.py

./build/examples/xbar-cli /dev/ttysB 0 -- 3 0 0 1
```

The scenario replies to the topology request with a mother-only board, accepts
the reset, checks the `ProgramXbar` targets a daughterboard, and acknowledges
`StopProgramming` with `complete_count = 1`.

> The driver's `doProgramming` path is still a stub in the current refactor, and
> its topology database has no builtin daughterboard templates, so a full
> `xbar-cli` run will not get past `doProgramming` yet. The scenario is written
> to match the intended wire flow and can be run today against any host program
> that speaks the same protocol.

## Writing a new scenario

```python
import firmware_sim as sim

def check(cmd):
    assert cmd.prog.payload.WhichOneof("data") == "xbar"

STEPS = [
    ("topo",  None,  sim.topo_response()),
    ("reset", None,  None),
    ("prog",  check, None),
    ("stop",  None,  sim.stop_response(1)),
]
```

`request` may be a oneof field name, a partial `MotherCommand`, or a predicate;
`response` may be `None`, a `MotherResponse`, raw bytes, or a callable taking the
received command. Run with `--strict` to fail on trailing unexpected commands.
