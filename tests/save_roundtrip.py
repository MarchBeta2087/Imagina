#!/usr/bin/env python3
"""Save/load round trip for the SDL front-end.

The front-end can drive itself from the command line (--open and --save), so
this test does not need a mouse or a screenshot.  It covers the two saving
bugs that reached users:

  * saving before the reference task produced an orbit used to crash, and
  * a high magnification reference without a high-range LA stage used to throw
    while compressing the orbit.

Run it under a display; on Linux CI that means xvfb-run.  Only the standard
library is used.
"""

import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

MAGIC = 0x000A0D56504D49FF
HEADER_SIZE = 32


def fail(message):
    print("FAIL: " + message, file=sys.stderr)
    sys.exit(1)


def run(binary, arguments, timeout):
    command = [str(binary), *map(str, arguments)]
    print("+ " + " ".join(command), flush=True)
    try:
        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        fail("the program did not exit within %d s: %s" % (timeout, " ".join(command)))

    if result.returncode != 0:
        sys.stdout.write(result.stdout.decode("utf-8", "replace"))
        sys.stderr.write(result.stderr.decode("utf-8", "replace"))
        fail("exit code %d from %s" % (result.returncode, " ".join(command)))

    for stream in (result.stdout, result.stderr):
        text = stream.decode("utf-8", "replace")
        if text.strip():
            print(text, end="" if text.endswith("\n") else "\n")


def check_im(path, require_reference):
    if not path.is_file():
        fail("%s was not written" % path)

    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        fail("%s is only %d bytes" % (path, len(data)))

    magic, _reserved, location_offset, reference_offset = struct.unpack_from("<QQQQ", data, 0)
    if magic != MAGIC:
        fail("%s does not start with the .im magic number" % path)
    if not (HEADER_SIZE <= location_offset < len(data)):
        fail("%s has an invalid location offset %d" % (path, location_offset))
    if reference_offset >= len(data):
        fail("%s has an invalid reference offset %d" % (path, reference_offset))
    if require_reference and reference_offset == 0:
        fail("%s has no reference section" % path)

    temporary = path.with_name(path.name + ".tmp")
    if temporary.exists():
        fail("%s was left behind" % temporary)

    print(
        "%s: %d bytes, location at %d, reference at %d"
        % (path.name, len(data), location_offset, reference_offset)
    )


def write_high_zoom_imt(path):
    # Size is the half height, so 2e-40 is a magnification of 1e40.  Loading
    # this makes the evaluator generate a reference with no high-range LA stage,
    # which is the reference that used to throw in SaveCompressedOrbit.
    path.write_text(
        "Formula: Mandelbrot\n"
        "Location:\n"
        "\tRe: -0.743643887037151\n"
        "\tIm: 0.13182590420533\n"
        "\tSize: 2e-40\n"
        "\tIterations: 3000\n",
        encoding="utf-8",
    )


def fixture_path():
    # Saved at a magnification of 1e40.  Its reference has three LA stages and
    # the last one uses double precision, so it has no high-range orbit: this
    # is the reference that used to throw in SaveCompressedOrbit.
    return Path(__file__).resolve().parent / "data" / "high-zoom-reference.im"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "binary",
        nargs="?",
        default="build/Imagina",
        help="path to the SDL front-end (default: build/Imagina)",
    )
    parser.add_argument(
        "--workdir",
        help="directory for the generated files (default: a temporary directory)",
    )
    arguments = parser.parse_args()

    binary = Path(arguments.binary)
    if not binary.is_file():
        fail("%s does not exist; build the SDL front-end first" % binary)

    if arguments.workdir:
        work = Path(arguments.workdir)
        work.mkdir(parents=True, exist_ok=True)
        cleanup = None
    else:
        cleanup = tempfile.TemporaryDirectory(prefix="imagina-save-")
        work = Path(cleanup.name)

    try:
        start = work / "start.im"
        roundtrip = work / "roundtrip.im"
        high_imt = work / "high-zoom.imt"
        high_im = work / "high-zoom.im"
        high_roundtrip = work / "high-zoom-roundtrip.im"
        high_twice = work / "high-zoom-twice.im"

        # 1. Save the default view, then load that file and save it again.
        run(binary, ["--save", start], 120)
        check_im(start, require_reference=False)

        run(binary, ["--open", start, "--save", roundtrip], 120)
        check_im(roundtrip, require_reference=False)

        # 2. Generate a high magnification reference from the text format,
        #    then save it.  This is the path that used to throw while
        #    compressing the orbit.
        write_high_zoom_imt(high_imt)
        run(binary, ["--open", high_imt, "--save", high_im], 300)
        check_im(high_im, require_reference=True)

        # 3. Read the written file back: this exercises LoadCompressedOrbit and
        #    the element type selected from the header.
        run(binary, ["--open", high_im, "--save", high_roundtrip], 300)
        check_im(high_roundtrip, require_reference=True)

        # 4. The fixture was written on Windows before the exponent width was
        #    fixed; it must still load and save.
        fixture = fixture_path()
        if not fixture.is_file():
            fail("%s is missing" % fixture)
        run(binary, ["--open", fixture, "--save", high_twice], 300)
        check_im(high_twice, require_reference=True)

        print("save round trip passed")
    finally:
        if cleanup is not None:
            cleanup.cleanup()


if __name__ == "__main__":
    main()
