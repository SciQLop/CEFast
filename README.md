[![tests](https://github.com/SciQLop/CEFast/actions/workflows/tests.yml/badge.svg)](https://github.com/SciQLop/CEFast/actions/workflows/tests.yml)
[![wheels](https://github.com/SciQLop/CEFast/actions/workflows/wheels.yml/badge.svg)](https://github.com/SciQLop/CEFast/actions/workflows/wheels.yml)
[![PyPI](https://img.shields.io/pypi/v/cefast)](https://pypi.org/project/cefast/)
[![Python versions](https://img.shields.io/pypi/pyversions/cefast)](https://pypi.org/project/cefast/)
[![license](https://img.shields.io/badge/license-MIT-blue)](COPYING)

<p align="center"><img src="docs/images/cefast_logo.png" alt="CEFast" width="600"></p>

# CEFast

A fast reader for CEF (Cluster Exchange Format), the text format of ESA's Cluster Science Archive.
C++20 core, Python bindings, no dependencies beyond numpy.

```sh
pip install cefast
```

```python
import cefast
f = cefast.load("C1_CP_FGM_SPIN__20010201_000000_20010202_000000_V140305.cef.gz")
f["B_vec_xyz_gse__C1_CP_FGM_SPIN"].values      # (records, 3) float32
f["time_tags__C1_CP_FGM_SPIN"].values.dtype    # datetime64[ns]
```

## Why CEFast

CEFLIB is the reference CEF reader, and it is hard to live with: source tarball only, no wheels,
a global singleton, no working byte variables, and its maintainer has passed away. CEFast is
installable with pip, free of global state, and readable by someone else.

Correctness is checked, not claimed:

- every CEF 2.0 construct the archive uses, checked against the specification, with the deviations
  we accept listed in [docs/accepted-deviations.md](docs/accepted-deviations.md);
- 41 real CSA files, every one CEFLIB can read compared value by value against CEFLIB 1.8.6,
  plus hand-made files for malformed input;
- the full suite runs under ASan and UBSan in CI, together with an allocation budget test for the
  header path;
- a malformed file raises a `ParseError` naming the file, the line and the record: it never returns
  shifted columns or garbage.

## Install

```sh
pip install cefast
```

Wheels are built for Linux (x86_64, aarch64), macOS 13+ (x86_64, arm64) and Windows (x64,
ARM64), for CPython 3.10 and newer, including free-threaded builds. A Pyodide (wasm32) wheel is
built too. Elsewhere, `pip install` builds from the sdist with a C++20 compiler and meson.

## Loading

```python
cefast.load("file.cef")            # path or os.PathLike
cefast.load("file.cef.gz")         # gzip, detected from the content
cefast.load(data)                  # bytes, bytearray or memoryview
```

Loading releases the GIL. A path is memory-mapped, or read when the platform has no mmap, and bytes
are borrowed: the returned arrays are views on that memory, valid as long as the `CEF` object and
its variables live.

## Variables

```python
f.keys()                           # names, in file order
"B_vec_xyz_gse__C1_CP_FGM_SPIN" in f
v = f["B_vec_xyz_gse__C1_CP_FGM_SPIN"]
v.name, v.value_type               # 'B_vec_xyz_gse__C1_CP_FGM_SPIN', 'FLOAT'
v.sizes                            # (3,), the CEF SIZES attribute
v.is_nrv                           # True when the values are in the header (DATA), not per record
v.attributes                       # dict of the other variable attributes
f.attributes                       # dict of the global attributes
```

Attribute values are plain Python values: text and numbers come as one value or a list, times as
`datetime.datetime`, time ranges as a pair of them.

Values are numpy arrays. Record-varying variables get a record axis, header variables keep their
declared shape.

| CEF VALUE_TYPE | numpy dtype |
| --- | --- |
| FLOAT | float32 |
| DOUBLE | float64 |
| INT | int32 |
| BYTE | uint8 |
| CHAR | numpy unicode string, copied |
| ISO_TIME | datetime64[ns] |
| ISO_TIME_RANGE | datetime64[ns], one extra dimension of 2 |

Missing data stays raw for numeric variables (the `FILLVAL` number itself) and becomes `NaT` for
time variables.

## INCLUDE

`INCLUDE = "file.ceh"` is resolved, in this order, with `include_resolver` first when given:

```python
cefast.load("file.cef", include_paths=["/data/headers"])
cefast.load("file.cef", include_resolver=lambda name: headers.get(name))  # str or None
```

then the directory of the file being read, then `include_paths`, then the `CEFPATH` environment
variable, then the current directory (which is what CEFLIB does).

## Errors

Anything that would silently produce wrong numbers raises instead:

```python
try:
    cefast.load("broken.cef")
except cefast.ParseError as error:
    print(error)   # broken.cef:214: expected 31 values, got 30 (record 12)
```

`ParseError` subclasses `ValueError` and names the file, the line and, inside the data section, the
record.

## C++

Header-only C++20, same API shape:

```cpp
#include <cefast/cefast.hpp>

auto file = cefast::load("file.cef");
for (const auto& var : file.variables)
    std::cout << var.name << '\n';
```

With meson: `dependency('cefast', fallback: ['cefast', 'cefast_dep'])`.

## Performance

On the 41-file CSA sample corpus (10.4 MB), against CEFLIB 1.8.6: **34.9 ms against 37.2 ms**
(1.07x overall, median 1.19x per file). Single files vary from 0.82x to 1.90x; gzipped files are
limited by decompression. Timings are machine-dependent; `tools/ceflib/benchmark.py` reproduces
them.

## Tests

```sh
python -I tools/fetch_csa_corpus.py    # downloads the CSA sample corpus and verifies its hashes
meson setup build && meson test -C build
```

The corpus is not in the repository: it is fetched from the archive and checked against
`tests/resources/csa_manifest.json`. Tests that need it skip when it is missing.
`tools/ceflib/benchmark.py` builds CEFLIB to compare against it; the differential test skips when
CEFLIB is not importable.

Known differences from CEFLIB and from the strict CEF 2.0 document are listed in
[docs/accepted-deviations.md](docs/accepted-deviations.md).

## License

MIT, see [COPYING](COPYING).
