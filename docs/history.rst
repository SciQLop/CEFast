=======
History
=======

Unreleased
==========

- ``print`` of a file shows a summary: name, record count, a table of the variables (type, shape,
  units, description) and the global attributes. ``print`` of a variable shows its type, shape
  and attributes.
- ``Variable.shape``: the shape of the values, without building the array.
- ``CEF`` is a full read-only mapping: ``values()``, ``items()`` and ``get()`` work, and
  ``isinstance(f, collections.abc.Mapping)`` is true.

0.1.1 (2026-11-06)
==================

- Fix a crash in Pyodide (JupyterLite, the browser): with Chromium, reading the first ``.values``
  could abort Python with "memory access out of bounds".
- Docstrings for the whole Python API, and a documentation site with examples you can run in the
  browser.

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
