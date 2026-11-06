============
Contributing
============

Bug reports, files that CEFast cannot read, and pull requests are welcome on
`GitHub <https://github.com/SciQLop/CEFast>`_. A file CEFast refuses or misreads is the most
useful report you can send: attach it, or give its dataset name and time range.

Build and test
==============

You need a C++20 compiler, `meson <https://mesonbuild.com>`_, ninja, and Python with numpy.

.. code-block:: console

    $ git clone https://github.com/SciQLop/CEFast
    $ cd CEFast
    $ python -I tools/fetch_csa_corpus.py
    $ meson setup build
    $ meson test -C build

``fetch_csa_corpus.py`` downloads the CSA sample corpus: 41 real archive files, checked against
the hashes in ``tests/resources/csa_manifest.json``. They are not stored in the repository.
Tests that need them are skipped when they are missing.

To use the module you just built, put the build directory first on ``PYTHONPATH``, and run
Python from outside the repository, so that the ``cefast/`` source folder does not hide it:

.. code-block:: console

    $ cd .. && PYTHONPATH=CEFast/build python -c "import cefast; print(cefast.__file__)"

The tests
=========

- C++ tests (Catch2) for the lexer, the header, the records, the values and times, and the
  whole corpus.
- Python tests for the bindings.
- A comparison with CEFLIB: every corpus file that CEFLIB can read is compared value by value.
  It is skipped when CEFLIB's Python module is not built: see ``tools/ceflib/build_ceflib.sh``.
- The same suite runs under AddressSanitizer and UndefinedBehaviorSanitizer in CI:

  .. code-block:: console

      $ meson setup build-asan -Db_sanitize=address,undefined -Db_lundef=false -Dpython=false
      $ meson test -C build-asan

A bug fix starts with a test that fails without it.

Fuzzing
=======

The fast parts of CEFast each have a slow, simple twin, and libFuzzer checks that both always
agree:

- ``fuzz_load``: serial against parallel loading of whole files;
- ``fuzz_float``: the number parser against ``strtod`` and fast_float;
- ``fuzz_time``: the fixed-format time parser against the general one;
- ``fuzz_lexer``, ``fuzz_lexer_diff``: the header lexer, and against a frozen copy of the older,
  simpler lexer;
- ``fuzz_records_diff``: the record reader against a frozen copy of the older, simpler one.

They need clang:

.. code-block:: console

    $ CXX=clang++ meson setup build-fuzz -Dfuzzing=true -Dpython=false -Dwith_tests=false
    $ ninja -C build-fuzz
    $ python3 tools/fuzz/make_corpus.py --output fuzz-corpus
    $ ./build-fuzz/tools/fuzz/fuzz_records_diff fuzz-corpus/records_diff -dict=tools/fuzz/cef.dict

CI runs each target for a minute on every push.

Performance work
================

A change made for speed needs numbers: instructions, cycles and branch misses per load from
``perf stat``, before and after, on a few corpus files. The driver is built with
``-Dwith_benchmarks=true``:

.. code-block:: console

    $ meson setup build-release -Dwith_benchmarks=true
    $ perf stat -e instructions,cycles,branch-misses \
        build-release/tools/bench/bench_load tests/resources/csa/C1_PP_DWP__*.cef 1000 1

Wall time alone is too noisy on small files. See :doc:`performance` for the comparison with
CEFLIB.

Documentation
=============

The documentation lives in ``docs/``. Build it, and run every Python example it contains:

.. code-block:: console

    $ python -m pip install -r docs/requirements.txt
    $ cd docs
    $ PYTHONPATH=../build python -m sphinx -W . _build/html
    $ PYTHONPATH=../build python check_examples.py

Sphinx runs from ``docs/`` for the same reason as above: from the repository root, Python would
import the ``cefast/`` source folder instead of the built module.

The examples download small files from the Cluster Science Archive, so the last command needs
network access. A block that cannot run as is is marked ``:class: no-check``.

Code style
==========

C++ is formatted with the repository's ``.clang-format``: run ``clang-format -i`` on the files
you change.
