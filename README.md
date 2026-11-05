<p align="center"><img src="docs/images/cefast_logo.png" alt="CEFast" width="600"></p>

# CEFast

CEF (Cluster Exchange Format) is the plain-text data format of ESA's Cluster Science Archive.
CEFast reads it, from C++ or Python, fast and without surprises.

**Status:** working and tested against 41 real CSA files, not released yet. Wheels are built for
Linux, macOS and Windows; the Python API is stable enough to be used by [speasy](https://github.com/SciQLop/speasy).

```python
import cefast

f = cefast.load("C1_CP_FGM_SPIN__20010201_000000_20010202_000000_V140305.cef.gz")
v = f["B_vec_xyz_gse__C1_CP_FGM_SPIN"]
v.values            # numpy array, shape (records, 3)
v.attributes["UNITS"]
f["time_tags__C1_CP_FGM_SPIN"].values.dtype   # datetime64[ns]
```

## Install

```sh
pip install cefast
```

numpy is the only dependency. Wheels cover CPython 3.10 and newer, including free-threaded builds.

## Loading

```python
cefast.load("file.cef")            # path or os.PathLike
cefast.load("file.cef.gz")         # gzip, detected from the content
cefast.load(data)                  # bytes, bytearray or memoryview
```

Loading releases the GIL. Files are memory-mapped when a path is given and borrowed when bytes are
passed, and the returned arrays are views on that memory: they stay valid as long as the `CEF`
object and its variables live.

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
