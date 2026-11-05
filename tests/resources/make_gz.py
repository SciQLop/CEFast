#!/usr/bin/env python3
"""Regenerate the committed gzip fixtures used by tests/cpp/test_input.cpp.

Run from this directory: python3 make_gz.py
Python's gzip is deliberately used instead of cefast's own gzip() so the decompressor is not
only tested against its mirror image. mtime=0 keeps the output byte-for-byte reproducible.
"""

import gzip
import pathlib

here = pathlib.Path(__file__).parent / "fixtures"
plain = (here / "hello.cef").read_bytes()
half = len(plain) // 2

(here / "hello.cef.gz").write_bytes(gzip.compress(plain, mtime=0))
(here / "two_members.cef.gz").write_bytes(
    gzip.compress(plain[:half], mtime=0) + gzip.compress(plain[half:], mtime=0)
)
(here / "trailing_zeros.cef.gz").write_bytes(
    (here / "hello.cef.gz").read_bytes() + b"\0" * 16
)
# Cut into the 8-byte CRC/ISIZE trailer: the member never completes.
(here / "truncated.cef.gz").write_bytes((here / "hello.cef.gz").read_bytes()[:-6])
