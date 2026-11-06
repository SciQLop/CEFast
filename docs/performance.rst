===========
Performance
===========

CEFast is about three times faster than CEFLIB, the reference CEF reader from IRAP, and faster
on every file we tried.

The numbers
===========

We load each file of the CSA sample corpus from Python: 41 real archive files, 10.4 MB in all,
from tiny caveat files to 1.8 MB particle distributions. Each file is loaded 21 times, after one
warm-up, and we keep the median time. CEFast runs with its default threads.

========================== ============ ============
                           CEFLIB 1.8.6 CEFast 0.1.0
========================== ============ ============
All 41 files               35.1 ms      11.5 ms
Speed-up, whole corpus                  3.05×
Speed-up, median file                   1.60×
Speed-up, slowest to best               1.20× to 7×
========================== ============ ============

Measured on a Ryzen 7 5800X (8 cores), Linux, 2026-11-06.

The gains are largest on big files, where CEFast parses the data section on several threads,
and smallest on small files, where most of the time goes into the header. Gzipped files are
limited by decompression.

Both readers convert every value: the comparison is fair. CEFLIB's own number parser is simple
and not correctly rounded; CEFast's numbers are exact (see :doc:`reading`).

Run it yourself
===============

From a clone of the repository, with CEFast built in ``build-release``, download the corpus
and build CEFLIB's Python module (under ``~/.cache/cefast-ceflib``, nothing is installed):

.. code-block:: console

    $ python -I tools/fetch_csa_corpus.py
    $ sh tools/ceflib/build_ceflib.sh

Then run the benchmark from the parent directory, so that Python imports the built module and
not the ``cefast/`` source folder:

.. code-block:: console

    $ cd ..
    $ PYTHONPATH=CEFast/build-release python -u CEFast/tools/ceflib/benchmark.py --runs 21

Where the speed comes from
==========================

A CEF file is text, so reading one is mostly turning text into numbers. CEFast does that in a
few ways:

- **One pass over the header.** The header is split into lines and tokens in one pass, without
  copying the text.
- **Exact numbers, cheaply.** Most numbers in archive files are short: ``13.498``, ``-1.0E31``,
  ``0``. They are converted with exact shortcuts (Clinger's fast path for decimals); the rare
  hard cases go to `fast_float <https://github.com/fastfloat/fast_float>`_. The result is always
  the correctly rounded number.
- **A fast path for normal records.** A record with plain values is read column by column, with
  the type known in advance. Anything unusual (quotes, comments, odd spacing) goes to the
  general parser, so both give the same result and the same errors.
- **Fixed-format times.** ``2005-06-10T00:00:04.000Z`` is read with a few SIMD instructions
  when it has the usual shape.
- **Several threads for large files.** A data section above about 48 kB is cut into pieces at
  record boundaries, and a pool of threads parses them. The pool starts once per process.

Each of these was measured with hardware counters (instructions, cycles, branch misses) and
kept only when it helped. The fast paths are checked against the simpler code they replace by
fuzzing: see :doc:`contributing`. The measurement logs are in
`docs/perf <https://github.com/SciQLop/CEFast/tree/main/docs/perf>`_.
