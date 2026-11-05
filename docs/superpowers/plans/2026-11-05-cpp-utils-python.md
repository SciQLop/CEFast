# cpp_utils_python Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create `cpp_utils/cpp_utils_python/` with the generic numpy and datetime helpers CEFast needs, moved out of pycdfpp, and switch pycdfpp to them.

**Architecture:** Header-only, opt-in by include (same model as `cpp_utils_qt/`): only code that includes these headers needs pybind11/CPython. Tested by a tiny pybind11 test module built only when `-Dpython=true`.

**Tech Stack:** C++20, pybind11, CPython datetime C API, meson, Catch2 not used here (Python-side tests with unittest).

**Spec:** `docs/superpowers/specs/2026-11-05-cefast-design.md` (section "Architecture", item 1).

## Global Constraints

- Repos: `~/Documents/prog/cpp_utils` (branch `feat/cpp-utils-python`), `~/Documents/prog/CDFpp` (branch `refactor/use-cpp-utils-python`). Never push; pushing needs explicit user consent.
- Header-only. No new mandatory dependency for cpp_utils: pybind11 only behind `option('python')`, default `false`, used for tests only.
- Moved code keeps behaviour: CDFpp's full test suite must pass (`meson test -C build` real pass count + exit code 0).
- Out of scope (stay in pycdfpp): GIL helpers and interpreter-exit guard (`remote.hpp`), list/tuple browsing, string helpers, everything CDF-typed (EPOCH/TT2000).
- One build/test invocation per build directory at a time, in the foreground.
- `.clang-format` of each repo (WebKit-based, 100 cols, Allman braces).

## Review Focus

1. Timezone-aware `datetime` input: `to_time_point` of a `+02:00` datetime must return the UTC instant (pycdfpp's `utc_offset` behaviour) — test in Task 2.
2. Naive `datetime` input is treated as UTC (pycdfpp behaviour) — test in Task 2.
3. Empty vector → numpy: shape `(0,)` or `(0, 3)` must not crash or leak — test in Task 1.
4. Pre-1970 instants: `to_datetime` of a negative ns count (e.g. 1958-01-01) — test in Task 2.
5. The array returned by `view_with_owner` must outlive the C++ scope that built it (no dangling) — test in Task 1 via `gc.collect()` after the producer returns.

---

### Task 1: numpy helpers

**Files:**
- Create: `include/cpp_utils/cpp_utils_python/numpy.hpp`
- Create: `tests/python/cpp_utils_python_test_module.cpp`, `tests/python/test_numpy.py`
- Create: `subprojects/pybind11.wrap` (copy CDFpp's)
- Modify: `meson.build` (headers list), `meson_options.txt` (`option('python', type: 'boolean', value: false)`), `tests/meson.build`: when the option is on, `python3 = import('python').find_installation('python3', modules: ['numpy'])`, `pybind11_dep = dependency('pybind11')`, numpy include from `run_command(python3, '-c', 'import numpy; print(numpy.get_include())', check: true)`, build the module, `test('python_numpy', python3, ...)` with `env: ['PYTHONPATH=' + meson.current_build_dir()]`; the whole python block is skipped when the option is off.

**Interfaces:**
- Produces (namespace `cpp_utils::python`):
  - `template <typename T, typename Shape, typename Owner = std::nullptr_t> pybind11::array_t<T> fast_allocate_array(const Shape& shape, Owner&& owner = nullptr)` — moved as is from `CDFpp/pycdfpp/collections.hpp:132-154`, using `cpp_utils::containers::default_init_allocator`.
  - **New code** (not in pycdfpp): `template <typename T> pybind11::array_t<T> view_with_owner(T* data, std::vector<ssize_t> shape, std::shared_ptr<const void> owner)` — zero-copy, writable, C-contiguous view; the array's base is a `py::capsule` holding a heap copy of `owner`, so the C++ data lives as long as any array viewing it.

- [ ] **Step 1: Write the failing test**

Test module `_cpp_utils_python_test` exposes `make_iota(n: int, cols: int) -> np.ndarray` (builds a `std::make_shared<std::vector<double>>` of `0..n*cols-1`, returns `view_with_owner(v->data(), {n, cols}, v)`) and `make_uninit(n: int) -> np.ndarray` (`fast_allocate_array<double>(std::array{n})`).

```python
class TestNumpy(unittest.TestCase):
    def test_view_values_and_shape(self):
        a = m.make_iota(4, 3)
        self.assertEqual(a.shape, (4, 3)); self.assertEqual(a.dtype, np.float64)
        np.testing.assert_array_equal(a.ravel(), np.arange(12.0))
        self.assertTrue(a.flags.writeable); a[0, 0] = 42.0; self.assertEqual(a[0, 0], 42.0)
    def test_array_outlives_producer(self):
        a = m.make_iota(1000, 3); gc.collect()
        self.assertEqual(a[-1, -1], 2999.0)
    def test_empty(self):
        self.assertEqual(m.make_iota(0, 3).shape, (0, 3))
    def test_fast_allocate_shape(self):
        self.assertEqual(m.make_uninit(5).shape, (5,))
```

- [ ] **Step 2: Run to verify it fails** — `meson setup -Dpython=true build-py && meson test -C build-py python_numpy` → FAIL (header missing).
- [ ] **Step 3: Implement `numpy.hpp`** (copy `fast_allocate_array` from pycdfpp, add `view_with_owner`).
- [ ] **Step 4: Run** — same command → PASS. Then full `meson test -C build-py` → all pass.
- [ ] **Step 5: Commit** — `feat(python): numpy helpers (fast_allocate_array, view_with_owner)`.

### Task 2: datetime helpers

**Files:**
- Create: `include/cpp_utils/cpp_utils_python/datetime.hpp`, `tests/python/test_datetime.py`
- Modify: `tests/python/cpp_utils_python_test_module.cpp`, `tests/meson.build`, `meson.build`

**Interfaces:**
- Produces (namespace `cpp_utils::python`), all moved from `CDFpp/pycdfpp/chrono.hpp` `_details` (generic parts only):
  - `PyObject* to_datetime(std::chrono::sys_time<std::chrono::nanoseconds> tp)` — new reference, naive UTC `datetime.datetime`, microsecond truncation (floor), as pycdfpp's `to_datetime(time_point)`.
  - `PyObject* to_datetime(int64_t ns_since_1970)`.
  - `std::chrono::sys_time<std::chrono::nanoseconds> to_time_point(const PyObject* dt)` — naive = UTC, aware → subtract `utcoffset()` (pycdfpp `to_tp` + `utc_offset`).
  - `pybind11::array as_datetime64_ns(pybind11::array_t<int64_t> ns)` — same buffer viewed with dtype `datetime64[ns]` (no copy; `.view`).
  - `struct py_datetime` + its `type_caster` (moved as is).
  - Each function calls `PyDateTime_IMPORT` lazily once (pycdfpp does it in module init; keep a `ensure_datetime_api()` helper so callers don't need to).

- [ ] **Step 1: Write the failing tests**

```python
def test_roundtrip_ns(self):
    self.assertEqual(m.ns_to_datetime(0), datetime(1970, 1, 1))
    self.assertEqual(m.ns_to_datetime(-378691200 * 10**9), datetime(1958, 1, 1))
def test_microsecond_floor(self):
    self.assertEqual(m.ns_to_datetime(1999), datetime(1970, 1, 1, 0, 0, 0, 1))
def test_aware_to_utc(self):
    dt = datetime(2001, 2, 1, 2, 0, tzinfo=timezone(timedelta(hours=2)))
    self.assertEqual(m.datetime_to_ns(dt), calendar.timegm((2001, 2, 1, 0, 0, 0)) * 10**9)
def test_naive_is_utc(self):
    self.assertEqual(m.datetime_to_ns(datetime(1970, 1, 2)), 86400 * 10**9)
def test_datetime64_view(self):
    a = m.as_dt64(np.array([0, 10**9], dtype=np.int64))
    self.assertEqual(a.dtype, np.dtype('datetime64[ns]'))
    self.assertEqual(a[1], np.datetime64('1970-01-01T00:00:01', 'ns'))
```

- [ ] **Step 2: Run to verify it fails** — `meson test -C build-py python_datetime` → FAIL.
- [ ] **Step 3: Implement `datetime.hpp`** by moving the generic code; no CDF types.
- [ ] **Step 4: Run** — `meson test -C build-py` → all pass.
- [ ] **Step 5: Commit** — `feat(python): datetime helpers moved from pycdfpp`.

### Task 3: switch pycdfpp to cpp_utils_python

**Files:**
- Modify: `CDFpp/pycdfpp/collections.hpp` (delete `fast_allocate_array` overload that takes a shape; keep the `py::buffer_info` / `py::array` convenience overloads as thin calls into `cpp_utils::python::fast_allocate_array`), `CDFpp/pycdfpp/chrono.hpp` (delete moved code, `using` / call the cpp_utils versions; CDF-typed overloads stay).
- Modify: `CDFpp/subprojects/cpp_utils.wrap` only if needed; for local work, point `subprojects/cpp_utils` at the local cpp_utils branch (symlink or `meson subprojects` packagefile) and say so in the commit message. The wrap revision bump happens after cpp_utils is pushed (user consent).

**Interfaces:**
- Consumes: Task 1 and Task 2 functions.
- Produces: no API change in pycdfpp.

- [ ] **Step 1: Baseline** — on CDFpp `main`, `meson test -C build` → record pass count (expected: all pass).
- [ ] **Step 2: Replace the code** as above. No behaviour change, no new tests (the existing `python_chrono`, `python_loading`, `python_value_conversions` cover it).
- [ ] **Step 3: Run** — `ninja -C build && meson test -C build` → same pass count, exit code 0.
- [ ] **Step 4: Commit** — `refactor(pycdfpp): use cpp_utils_python numpy/datetime helpers`.
