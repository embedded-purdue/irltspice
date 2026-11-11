#!/usr/bin/env python3
"""Model the TSPICE firmware on one end of a socat PTY for integration tests.

The harness speaks the same framed protocol as the firmware and the host-side
driver (``firmware/include/common/framing.h``): it reads ``MotherCommand``
messages from a tty, checks each one against an expected step, and writes back
``MotherResponse`` messages. This lets a real host program -- for example
``examples/xbar_cli.cpp`` -- be exercised without a board, by scripting the
board's replies.

A *scenario* is a Python file exposing ``STEPS`` (or ``SCENARIO``): a list of
``(request, validate, response)`` tuples.

    request   Which command to expect. One of:
                - a string: the ``MotherCommand`` oneof field name, i.e. one of
                  ``"topo"``, ``"prog"``, ``"stop"``, ``"reset"``
                - a partial ``MotherCommand``: every field it sets must match
                - a callable ``(cmd) -> bool``: arbitrary predicate
    validate  Optional callable ``(cmd) -> bool | None`` run once *request*
              matches. Raising an exception or returning False fails the step.
    response  What to send back. One of ``None``, a ``MotherResponse``, raw
              ``bytes``, or a callable ``(cmd) -> MotherResponse | bytes | None``.

The protobuf Python bindings are generated with ``protoc`` from
``proto/command.proto`` into a cache directory on first use.

Example scenario for the crossbar CLI::

    # tests/scenarios/xbar_cli.py
    import firmware_sim as sim

    def is_xbar_to_daughter(cmd):
        assert cmd.prog.payload.WhichOneof("data") == "xbar"
        assert cmd.prog.daughterboard_id != sim.NO_DAUGHTERBOARD

    STEPS = [
        ("topo",  None,                  sim.topo_response()),
        ("reset", None,                  None),
        ("prog",  is_xbar_to_daughter,   None),
        ("stop",  None,                  sim.stop_response(1)),
    ]

Run it against the example::

    # 1. Create a PTY pair (socat prints the two slave paths).
    socat -d -d pty,raw,echo=0 pty,raw,echo=0

    # 2. Attach the simulated firmware to one end ...
    python3 tests/firmware_sim.py --tty /dev/ttysAAA tests/scenarios/xbar_cli.py

    # 3. ... and the program under test to the other.
    ./build/examples/xbar-cli /dev/ttysBBB 0 -- 3 0 0 1
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import select
import shutil
import subprocess
import sys
import termios
import time
from pathlib import Path
from typing import Callable, Iterable, Optional, Sequence, Union

# =====================================================================
# Paths and protocol constants
# =====================================================================

THIS = Path(__file__).resolve()
TESTS_DIR = THIS.parent  # software/irlc/tests
IRLC_DIR = TESTS_DIR.parent  # software/irlc
REPO_ROOT = TESTS_DIR.parents[2]  # repo root
PROTO_DIR = REPO_ROOT / "proto"
DEFAULT_PROTO = PROTO_DIR / "command.proto"
DEFAULT_GEN_DIR = TESTS_DIR / ".cache" / "pbgen"

START_BYTE = 0xAB
# irl::NO_DAUGHTERBOARD (std::numeric_limits<uint8_t>::max()).
NO_DAUGHTERBOARD = 0xFF
DEFAULT_BAUD = 115200

# Filled in by main() once the protobuf bindings are importable.
pb2 = None


# =====================================================================
# protobuf bindings
# =====================================================================


def generate_bindings(
    out_dir: Path = DEFAULT_GEN_DIR,
    proto: Path = DEFAULT_PROTO,
    include: Path = PROTO_DIR,
    protoc: str = "protoc",
    force: bool = False,
) -> Path:
    """Run protoc to produce ``<proto>_pb2.py`` in *out_dir*; return its path."""
    out_dir.mkdir(parents=True, exist_ok=True)
    target = out_dir / f"{proto.stem}_pb2.py"

    if not force and target.exists() and target.stat().st_mtime >= proto.stat().st_mtime:
        return target

    protoc_path = shutil.which(protoc)
    if protoc_path is None:
        raise SystemExit(
            f"error: could not find '{protoc}'. Install the protobuf compiler "
            f"(e.g. `brew install protobuf`) or pass --protoc."
        )

    cmd = [protoc_path, f"--python_out={out_dir}", f"-I{include}", str(proto)]
    print(f"[sim] generating bindings: {' '.join(cmd)}", file=sys.stderr)
    try:
        subprocess.run(cmd, check=True)
    except subprocess.CalledProcessError as exc:
        raise SystemExit(f"error: protoc failed with exit code {exc.returncode}") from exc
    return target


def import_bindings(gen_dir: Path):
    """Import (and cache) the generated ``command_pb2`` module."""
    global pb2
    if pb2 is not None:
        return pb2

    gen_dir = gen_dir.resolve()
    if str(gen_dir) not in sys.path:
        sys.path.insert(0, str(gen_dir))

    try:
        import command_pb2  # type: ignore
    except Exception as exc:  # ImportError or a protobuf runtime VersionError
        raise SystemExit(
            "error: could not import the generated protobuf bindings.\n"
            "       Make sure the `protobuf` Python package is installed and that\n"
            "       its version matches your `protoc` (see tests/requirements.txt):\n"
            f"         {exc}"
        ) from exc

    pb2 = command_pb2
    return pb2


# =====================================================================
# Response helpers (for scenario files)
# =====================================================================


def _require_bindings():
    if pb2 is None:
        raise RuntimeError(
            "firmware_sim response helpers can only be used from a scenario "
            "loaded by firmware_sim.main()"
        )
    return pb2


def topo_response(
    unique_id: int = 0xC0FFEE,
    hardware_revision: int = 0,
    firmware: tuple[int, int, int] = (1, 0, 0),
    daughters: Optional[Sequence[Optional[tuple[str, int]]]] = None,
) -> "pb2.MotherResponse":
    """A ``MotherResponse`` carrying a single motherboard topology.

    ``daughters`` describes the four fixed slots: ``None`` means an empty slot,
    a ``(info_field, unique_id)`` pair populates it, where ``info_field`` is one
    of ``"basic_daughter"``, ``"diode_daughter"`` or ``"test_daughter"``.

    Note: the driver's topology database currently only has a builtin template
    for mother rev 0, so populated daughter slots will make it abort until
    daughter templates are registered. The default (all empty) is safe.
    """
    proto = _require_bindings()
    resp = proto.MotherResponse()
    mother = resp.topo.motherboards.add()
    mother.unique_id = unique_id
    mother.hardware_revision = hardware_revision
    mother.firm_version_major, mother.firm_version_minor, mother.firm_version_patch = firmware

    slots: Sequence[Optional[tuple[str, int]]] = daughters or [None, None, None, None]
    if len(slots) != 4:
        raise ValueError(f"expected 4 daughter slots, got {len(slots)}")
    for slot_desc in slots:
        slot = mother.daughterboards.add()
        if slot_desc is None:
            slot.empty.SetInParent()
        else:
            info_field, d_unique_id = slot_desc
            info = slot.daughterboard_info
            info.unique_id = d_unique_id
            info.firm_version_major = 1
            info.firm_version_minor = 0
            info.firm_version_patch = 0
            getattr(info, info_field).SetInParent()
    return resp


def stop_response(complete_count: int = 1) -> "pb2.MotherResponse":
    """A ``MotherResponse`` that acknowledges a ``StopProgramming`` command."""
    proto = _require_bindings()
    resp = proto.MotherResponse()
    resp.stop.complete_count = complete_count
    return resp


# =====================================================================
# Framed transport over a tty
# =====================================================================


class FirmwareTty:
    """A raw, framed link to a (socat-created) PTY."""

    def __init__(self, path: str, baud: int = DEFAULT_BAUD):
        self.path = path
        self._frames: list[bytes] = []
        self._reset_parser()
        try:
            self._fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        except OSError as exc:
            raise SystemExit(f"error: could not open tty '{path}': {exc}") from exc
        try:
            self._configure(baud)
        except Exception:
            os.close(self._fd)
            raise

    # -- lifecycle ----------------------------------------------------

    def close(self) -> None:
        if getattr(self, "_fd", None) is not None:
            os.close(self._fd)
            self._fd = None

    def __enter__(self) -> "FirmwareTty":
        return self

    def __exit__(self, *exc) -> None:
        self.close()

    # -- setup --------------------------------------------------------

    def _configure(self, baud: int) -> None:
        try:
            baud_const = getattr(termios, f"B{baud}")
        except AttributeError as exc:
            raise SystemExit(f"error: unsupported baud rate {baud}") from exc

        attrs = termios.tcgetattr(self._fd)
        iflag, oflag, cflag, lflag, ispeed, ospeed, cc = attrs
        iflag = 0
        oflag = 0
        lflag = 0
        cflag &= ~(termios.PARENB | termios.CSTOPB | termios.CSIZE | termios.CRTSCTS)
        cflag |= termios.CS8 | termios.CREAD | termios.CLOCAL
        cc[termios.VMIN] = 0
        cc[termios.VTIME] = 0
        termios.tcsetattr(
            self._fd,
            termios.TCSANOW,
            [iflag, oflag, cflag, lflag, baud_const, baud_const, cc],
        )
        termios.tcflush(self._fd, termios.TCIOFLUSH)

    # -- framing ------------------------------------------------------

    def _reset_parser(self) -> None:
        self._phase = "start"
        self._length = 0
        self._length_check = 0
        self._cnt = 0
        self._payload = bytearray()
        self._crc = 0

    def _feed(self, data: bytes) -> None:
        for b in data:
            phase = self._phase
            if phase == "start":
                if b == START_BYTE:
                    self._phase = "length"
                    self._length = 0
                    self._cnt = 2
            elif phase == "length":
                self._length = (self._length << 8) | b
                self._cnt -= 1
                if self._cnt == 0:
                    self._phase = "length_check"
                    self._length_check = 0
                    self._cnt = 2
            elif phase == "length_check":
                self._length_check = (self._length_check << 8) | b
                self._cnt -= 1
                if self._cnt == 0:
                    if self._length != self._length_check:
                        self._phase = "start"
                    elif self._length == 0:
                        self._phase = "crc"
                        self._cnt = 2
                        self._crc = 0
                        self._payload = bytearray()
                    else:
                        self._phase = "data"
                        self._cnt = self._length
                        self._payload = bytearray()
            elif phase == "data":
                self._payload.append(b)
                self._cnt -= 1
                if self._cnt == 0:
                    self._phase = "crc"
                    self._cnt = 2
                    self._crc = 0
            elif phase == "crc":
                self._crc = (self._crc << 8) | b
                self._cnt -= 1
                if self._cnt == 0:
                    self._frames.append(bytes(self._payload))
                    self._phase = "start"
                    if self._crc != 0:
                        print(
                            f"[sim] warning: frame with crc=0x{self._crc:04x}",
                            file=sys.stderr,
                        )

    def read_frame(self, timeout: float) -> Optional[bytes]:
        """Return the next complete frame payload, or None on timeout/EOF."""
        deadline = time.monotonic() + timeout
        while True:
            if self._frames:
                return self._frames.pop(0)
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            ready, _, _ = select.select([self._fd], [], [], remaining)
            if not ready:
                return None
            try:
                data = os.read(self._fd, 4096)
            except BlockingIOError:
                continue
            except OSError:
                return None
            if not data:
                return None
            self._feed(data)

    def write_frame(self, payload: bytes) -> None:
        n = len(payload)
        frame = (
            bytes([START_BYTE, (n >> 8) & 0xFF, n & 0xFF, (n >> 8) & 0xFF, n & 0xFF])
            + payload
            + b"\x00\x00"
        )
        view = memoryview(frame)
        while view:
            try:
                written = os.write(self._fd, view)
            except BlockingIOError:
                select.select([], [self._fd], [], 1.0)
                continue
            view = view[written:]


# =====================================================================
# Scenario handling
# =====================================================================

Request = Union[str, Callable, object]
Validate = Optional[Callable]
Response = Union[None, bytes, Callable, object]
Step = tuple[Request, Validate, Response]


def _is_repeated(field) -> bool:
    if hasattr(field, "is_repeated"):
        return bool(field.is_repeated)
    return field.label == field.LABEL_REPEATED


def _is_message(field) -> bool:
    if getattr(field, "message_type", None) is not None:
        return True
    type_message = getattr(field, "TYPE_MESSAGE", None)
    return type_message is not None and getattr(field, "type", None) == type_message


def _partial_match(expected, actual) -> bool:
    """True if every field explicitly set on *expected* matches *actual*."""
    for field, value in expected.ListFields():
        if _is_repeated(field):
            if list(getattr(actual, field.name)) != list(value):
                return False
        elif _is_message(field):
            if not _partial_match(value, getattr(actual, field.name)):
                return False
        elif getattr(actual, field.name) != value:
            return False
    return True


def request_matches(request: Request, cmd) -> bool:
    if request is None:
        return True
    if isinstance(request, str):
        return cmd.WhichOneof("cmd") == request
    if isinstance(request, (bytes, bytearray)):
        raise TypeError("request may not be raw bytes; use a string or predicate")
    if hasattr(request, "DESCRIPTOR"):  # a protobuf message
        return _partial_match(request, cmd)
    if callable(request):
        return bool(request(cmd))
    raise TypeError(f"unsupported request matcher: {request!r}")


def _describe(cmd) -> str:
    from google.protobuf import text_format

    oneof = cmd.WhichOneof("cmd")
    text = text_format.MessageToString(cmd, as_one_line=False).strip()
    return text or f"<empty MotherCommand ({oneof})>"


def _as_payload(response) -> Optional[bytes]:
    if response is None:
        return None
    if isinstance(response, (bytes, bytearray)):
        return bytes(response)
    if hasattr(response, "SerializeToString"):
        return response.SerializeToString()
    raise TypeError(f"unsupported response: {response!r}")


def load_scenario(path: Path):
    """Import a scenario file and return the module."""
    path = path.resolve()
    if not path.exists():
        raise SystemExit(f"error: scenario file not found: {path}")

    # Scenario files typically `import firmware_sim as sim`; make sure this
    # directory is importable regardless of the current working directory, and
    # that the import resolves to *this* module (which holds the loaded
    # bindings in its globals) rather than a second copy under a new name.
    if str(TESTS_DIR) not in sys.path:
        sys.path.insert(0, str(TESTS_DIR))
    # Let scenarios import helper modules from their own directory.
    if str(path.parent) not in sys.path:
        sys.path.insert(0, str(path.parent))
    sys.modules.setdefault("firmware_sim", sys.modules[__name__])

    spec = importlib.util.spec_from_file_location(path.stem, path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"error: could not load scenario module from {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[path.stem] = module
    spec.loader.exec_module(module)
    return module


def get_steps(module) -> list[Step]:
    for name in ("STEPS", "SCENARIO"):
        steps = getattr(module, name, None)
        if steps is not None:
            return list(steps)
    raise SystemExit(
        f"error: scenario '{module.__name__}' must define a STEPS (or SCENARIO) list"
    )


# =====================================================================
# The run loop
# =====================================================================


class ScenarioError(Exception):
    pass


def run_scenario(
    tty: FirmwareTty,
    steps: Sequence[Step],
    timeout: float = 5.0,
    verbose: bool = True,
    strict: bool = False,
) -> int:
    proto = _require_bindings()

    for i, (request, validate, response) in enumerate(steps):
        label = request if isinstance(request, str) else type(request).__name__
        if verbose:
            print(f"[sim] step {i}: expecting {label!r} ...", file=sys.stderr)

        raw = tty.read_frame(timeout)
        if raw is None:
            raise ScenarioError(f"step {i}: timed out waiting for a command ({label!r})")

        cmd = proto.MotherCommand()
        if not cmd.ParseFromString(raw):
            raise ScenarioError(f"step {i}: could not parse a MotherCommand")

        if verbose:
            for line in _describe(cmd).splitlines():
                print(f"[sim]   <- {line}", file=sys.stderr)

        if not request_matches(request, cmd):
            raise ScenarioError(
                f"step {i}: got 'cmd={cmd.WhichOneof('cmd')}' but expected {label!r}\n"
                f"{_describe(cmd)}"
            )

        if validate is not None:
            result = validate(cmd)
            if result is False:
                raise ScenarioError(f"step {i}: validator rejected the command")

        resolved = response(cmd) if callable(response) and not hasattr(response, "DESCRIPTOR") else response
        payload = _as_payload(resolved)
        if payload is not None:
            tty.write_frame(payload)
            if verbose:
                print(f"[sim]   -> {len(payload)} bytes", file=sys.stderr)

    if strict:
        extra = tty.read_frame(timeout)
        if extra is not None:
            cmd = proto.MotherCommand()
            cmd.ParseFromString(extra)
            raise ScenarioError(
                f"received an unexpected extra command after the scenario:\n{_describe(cmd)}"
            )

    return 0


def simulate(
    tty_path: str,
    steps: Sequence[Step],
    baud: int = DEFAULT_BAUD,
    **run_kwargs,
) -> int:
    """Programmatic entry point: run *steps* against the tty at *tty_path*.

    Bindings must already be loaded (by :func:`import_bindings`). For a list of
    steps defined in a file, use :func:`load_scenario` + :func:`get_steps`.
    """
    with FirmwareTty(tty_path, baud=baud) as tty:
        return run_scenario(tty, steps, **run_kwargs)


# =====================================================================
# CLI
# =====================================================================


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("scenario", type=Path, help="scenario file exposing STEPS")
    parser.add_argument(
        "--tty", required=True, help="path to the tty (socat PTY) to attach to"
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="seconds to wait for each command (default: %(default)s)",
    )
    parser.add_argument(
        "--baud", type=int, default=DEFAULT_BAUD, help="baud rate (default: %(default)s)"
    )
    parser.add_argument(
        "--proto", type=Path, default=DEFAULT_PROTO, help="path to command.proto"
    )
    parser.add_argument(
        "--gen-dir",
        type=Path,
        default=DEFAULT_GEN_DIR,
        help="where to cache generated bindings (default: %(default)s)",
    )
    parser.add_argument(
        "--protoc", default="protoc", help="protoc executable to use"
    )
    parser.add_argument(
        "--regen", action="store_true", help="force regeneration of bindings"
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="fail if an unexpected command arrives after the last step",
    )
    parser.add_argument(
        "-q", "--quiet", action="store_true", help="suppress per-step logging"
    )
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)

    generate_bindings(
        out_dir=args.gen_dir,
        proto=args.proto.resolve(),
        include=args.proto.resolve().parent,
        protoc=args.protoc,
        force=args.regen,
    )
    import_bindings(args.gen_dir)

    module = load_scenario(args.scenario)
    steps = get_steps(module)

    print(
        f"[sim] attached to {args.tty}; running {len(steps)} step(s) from "
        f"{args.scenario}",
        file=sys.stderr,
    )

    try:
        with FirmwareTty(args.tty, baud=args.baud) as tty:
            run_scenario(
                tty,
                steps,
                timeout=args.timeout,
                verbose=not args.quiet,
                strict=args.strict,
            )
    except ScenarioError as exc:
        print(f"[sim] FAILED: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("[sim] interrupted", file=sys.stderr)
        return 130

    print("[sim] scenario complete", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
