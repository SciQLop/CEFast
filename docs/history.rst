=======
History
=======

0.1.0 (2026-11-06)
==================

First release.

- Reads CEF 2.0 files, plain or gzipped, from a path or from bytes, into numpy arrays: FLOAT,
  DOUBLE, INT, BYTE, CHAR, ISO_TIME as ``datetime64[ns]``, and ISO_TIME_RANGE.
- Global and variable attributes as plain Python values.
- ``INCLUDE`` files are found like CEFLIB finds them, plus ``include_paths`` and an
  ``include_resolver`` function.
- Releases the GIL. Large data sections are parsed on several threads (``threads=``).
- A malformed file raises ``ParseError``, naming the file, the line and the record.
- A C++20 library made only of headers, with the same features.
- Wheels for Linux, macOS and Windows (x86_64 and ARM64), CPython 3.10 and newer including
  free-threaded builds, and Pyodide.
