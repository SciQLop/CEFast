=======================
FAQ and troubleshooting
=======================

Find your problem by its symptom. If it's not here, please
`open an issue <https://github.com/SciQLop/CEFast/issues>`_.

Errors
======

``expected 31 values, got 30 (record 12)``
------------------------------------------

A record does not have one value for each record-varying variable. The file is damaged, often
truncated, or a variable's ``SIZES`` does not match what the records hold. The line number in
the message points at the record: open the file there.

``INCLUDE file not found: CL_CH_MISSION.ceh``
---------------------------------------------

The header includes another file that CEFast cannot find. Archive files include headers shared
by a mission or instrument. Put them next to the file, or tell CEFast where they are with
``include_paths=[...]`` or the ``CEFPATH`` environment variable. See
:ref:`reading:INCLUDE files`.

``unsupported CEF version: CEF-1.0``
------------------------------------

CEFast reads CEF 2.0 files only. Version 1 is an older format that is not compatible with
version 2, and the Cluster Science Archive never served it.

``invalid FLOAT value: 12abc``
------------------------------

A value is not a number of its variable's type. CEFast refuses to guess: CEFLIB would have
read ``12abc`` as ``12``. Check the file at the line the message gives.

Data
====

My averages are huge negative numbers
-------------------------------------

The data contains fill values, usually ``-1e30`` or ``-1e31``. CEFast keeps them as written.
Replace them with ``NaN`` first: see :ref:`cookbook:Replace fill values with NaN`.

Why is ``SIGNIFICANT_DIGITS`` a string?
---------------------------------------

Attribute entries are text unless the file gives them a ``VALUE_TYPE``. Text that looks like a
number stays text, so that ``"090213"`` or ``"1.10"`` keep their exact spelling. Convert it
yourself: ``int(attrs["SIGNIFICANT_DIGITS"])``. ``FILLVAL`` is the exception: it has the type of
its variable.

Why do variable names end with ``__C1_CP_FGM_SPIN``?
----------------------------------------------------

That is how the archive names them: the variable, two underscores, then the dataset. CEFast
keeps names as the file writes them, so they match ``DEPEND_0`` and the other attributes that
refer to them.

Can CEFast write CEF files?
---------------------------

No, it only reads them.

The archive also has my data as CDF
-----------------------------------

The Cluster Science Archive can deliver CDF files too. To read them, use
`pycdfpp <https://pycdfpp.readthedocs.io>`_, from the same authors.

Coming from CEFLIB
==================

CEFLIB is the CEF reader from IRAP, written in C with a Python module. It is old and no longer
maintained. Here is how its Python calls map to CEFast:

================================== ==============================================
CEFLIB                             CEFast
================================== ==============================================
``ceflib.read(path)``              ``f = cefast.load(path)``
``ceflib.varnames()``              ``list(f)``
``ceflib.var(name)``               ``f[name].values``
``ceflib.vattrs(name)``            ``f[name].attributes``
``ceflib.gattrs()``                ``f.attributes``
``ceflib.records()``               ``len(f[name].values)`` for a record-varying variable
``ceflib.record_varying(name)``    ``not f[name].is_nrv``
``ceflib.close()``                 nothing: the file is released when unused
================================== ==============================================

What changes:

- **Several files at once.** CEFLIB holds one file at a time, in global state. CEFast returns
  a value: keep as many files as you like, and load them from several threads.
- **Times** are ``datetime64[ns]``, not milliseconds since 1958 in a ``float64``. Nothing is
  lost below the millisecond.
- **Variable names** keep their ``__DATASET`` suffix. CEFLIB removes it from names and from
  every attribute value.
- **Constants** in ``DELTA_PLUS`` and ``DELTA_MINUS`` stay attributes. CEFLIB turns each into an
  extra variable called ``K001``, ``K002``, and so on.
- **Global attributes** are all there: CEFLIB's ``gattrs()`` gives four of them.
- **BYTE variables** can be read.
- **Numbers** are correctly rounded, and a malformed value is an error.

The full list of differences, with the reasons, is in :doc:`deviations`.
