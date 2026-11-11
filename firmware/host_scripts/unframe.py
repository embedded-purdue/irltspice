#!/usr/bin/env python3
"""Strip IRLTSPICE UART framing from a byte stream on stdin.

Reads framed bytes from stdin and writes the payload of each valid frame to
stdout, so the raw protobuf messages can be piped into a decoder. All
diagnostics go to stderr, keeping stdout a clean binary stream.

Wire format (see ``firmware/include/common/framing.h``)::

    +------+--------+--------+-----------+--------+
    | 0xAB | len_hi | len_lo | payload   | crc_hi |
    |      | len_hi | len_lo | (len B)   | crc_lo |
    +------+--------+--------+-----------+--------+

The 16-bit length is sent twice and must match; the trailing crc16 is a
placeholder that is currently always zero. After a length mismatch the parser
resumes scanning for the next 0xAB, mirroring the firmware state machine.

Typical use, tapping a socat PTY::

    cat /dev/ttys042 | python unframe.py -v | protoc --decode=MotherCommand \\
        -I proto/ proto/command.proto

Note that ``protoc --decode`` reads a single message, so a stream of frames
concatenated on stdout decodes as one merged message (last-wins for oneofs).
For per-message decoding, run the decoder once per frame -- e.g. via a small
shell loop -- or use ``--stats``/``-v`` to at least see frame boundaries.
"""

from __future__ import annotations

import argparse
import sys
from typing import Iterator, Tuple

START_BYTE = 0xAB


def iter_frames(stream, chunk_size: int = 65536) -> Iterator[Tuple[bytes, int]]:
    """Yield ``(payload, crc)`` for every completed frame in *stream*.

    *stream* is a binary file object. The state machine mirrors
    ``_process_byte`` in ``firmware/src/common/framing.c``; a zero-length
    payload is handled by jumping straight to the crc field.
    """
    phase = "start"
    length = 0
    length_check = 0
    cnt = 0
    payload = bytearray()
    crc = 0

    read = getattr(stream, "read1", stream.read)
    while True:
        chunk = read(chunk_size)
        if not chunk:
            return
        for b in chunk:
            if phase == "start":
                if b == START_BYTE:
                    phase = "length"
                    length = 0
                    cnt = 2
            elif phase == "length":
                length = (length << 8) | b
                cnt -= 1
                if cnt == 0:
                    phase = "length_check"
                    length_check = 0
                    cnt = 2
            elif phase == "length_check":
                length_check = (length_check << 8) | b
                cnt -= 1
                if cnt == 0:
                    if length != length_check:
                        phase = "start"
                    elif length == 0:
                        phase = "crc"
                        cnt = 2
                        crc = 0
                        payload = bytearray()
                    else:
                        phase = "data"
                        cnt = length
                        payload = bytearray()
            elif phase == "data":
                payload.append(b)
                cnt -= 1
                if cnt == 0:
                    phase = "crc"
                    cnt = 2
                    crc = 0
            elif phase == "crc":
                crc = (crc << 8) | b
                cnt -= 1
                if cnt == 0:
                    yield bytes(payload), crc
                    phase = "start"


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true",
        help="log each frame to stderr",
    )
    parser.add_argument(
        "--keep-corrupt", action="store_true",
        help="emit frames whose crc is nonzero (default: drop them)",
    )
    parser.add_argument(
        "--stats", action="store_true",
        help="print frame/byte counts to stderr at end of input",
    )
    args = parser.parse_args(argv)

    out = sys.stdout.buffer
    good = 0
    good_bytes = 0
    bad = 0

    for payload, crc in iter_frames(sys.stdin.buffer):
        if crc != 0 and not args.keep_corrupt:
            bad += 1
            if args.verbose:
                print(
                    f"dropped {len(payload)}-byte frame with crc=0x{crc:04x}",
                    file=sys.stderr,
                )
            continue
        good += 1
        good_bytes += len(payload)
        if args.verbose:
            print(f"frame {good}: {len(payload)} bytes", file=sys.stderr)
        out.write(payload)
        out.flush()

    if args.stats:
        print(
            f"{good} frame(s), {good_bytes} payload byte(s), "
            f"{bad} dropped",
            file=sys.stderr,
        )

    return 0


if __name__ == "__main__":
    sys.exit(main())
