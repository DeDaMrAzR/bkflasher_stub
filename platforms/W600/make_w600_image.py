#!/usr/bin/env python3
"""Package the standalone W600 RD32 reader for the existing host loader."""

import sys
from pathlib import Path

PROTOCOL_MARKER = b"W600RD32"
MAXIMUM_READER_LENGTH = 0x1000 - 56 - 0x1A0


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: make_w600_image.py RAW_READER COPY_STUB OUTPUT")

    reader_path, copy_stub_path, output_path = map(Path, sys.argv[1:])
    if len(copy_stub_path.read_bytes()) != 80:
        raise SystemExit("The W600 secboot copy stub must be exactly 80 bytes.")

    image = reader_path.read_bytes() + PROTOCOL_MARKER
    if len(image) > MAXIMUM_READER_LENGTH:
        raise SystemExit("The W600 UART reader exceeds the one-sector bootstrap payload budget.")
    output_path.write_bytes(image)


if __name__ == "__main__":
    main()
