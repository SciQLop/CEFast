=============
Reading files
=============

This page covers everything about reading: where files come from, what you get back, and what
can go wrong. The examples use a small file written right here, so they run without a download:

.. code-block:: python

    import cefast

    content = b'''\
    FILE_NAME = "demo.cef"
    FILE_FORMAT_VERSION = "CEF-2.0"
    END_OF_RECORD_MARKER = "$"
    START_META = MISSION_TIME_SPAN
       VALUE_TYPE = ISO_TIME_RANGE
       ENTRY = 2000-07-16T00:00:00Z/2026-12-31T23:59:59Z
    END_META = MISSION_TIME_SPAN
    START_META = OBSERVATORY
       ENTRY = "Cluster-1"
    END_META = OBSERVATORY
    START_VARIABLE = time_tags
      VALUE_TYPE = ISO_TIME
      FILLVAL = 9999-12-31T23:59:59Z
    END_VARIABLE = time_tags
    START_VARIABLE = density
      VALUE_TYPE = FLOAT
      FILLVAL = -1.0E31
      SIGNIFICANT_DIGITS = 6
      DEPEND_0 = time_tags
    END_VARIABLE = density
    START_VARIABLE = flux
      VALUE_TYPE = DOUBLE
      SIZES = 2, 3
      DEPEND_0 = time_tags
    END_VARIABLE = flux
    START_VARIABLE = energy
      VALUE_TYPE = FLOAT
      SIZES = 3
      DATA = 10.0, 100.0, 1000.0
    END_VARIABLE = energy
    DATA_UNTIL = EOF
    2005-06-10T00:00:00Z, 3.5, 1,2,3,4,5,6 $
    2005-06-10T00:00:04Z, -1.0E31, 7,8,9,10,11,12 $
    9999-12-31T23:59:59Z, 3.7, 13,14,15,16,17,18 $
    '''
    f = cefast.load(content)

Loading
=======

:func:`cefast.load` reads a whole file and returns a :class:`~cefast.CEF`. It accepts:

- a path, as a :class:`str` or any :class:`os.PathLike` such as :class:`pathlib.Path`;
- the content of a file, as :class:`bytes`, :class:`bytearray` or :class:`memoryview`.

Gzipped files are recognised from their content, whatever their name:

.. code-block:: python

    import gzip

    f = cefast.load(gzip.compress(content))

Once :func:`~cefast.load` returns, the file is fully parsed and checked. Nothing is read
later, and the :class:`~cefast.CEF` does not depend on the file or the bytes you passed.

Variables
=========

A :class:`~cefast.CEF` works like a read-only dictionary of variables, in file order:

.. code-block:: python

    print(len(f))              # 4
    print(list(f))             # ['time_tags', 'density', 'flux', 'energy']
    print("density" in f)      # True
    density = f["density"]

Each :class:`~cefast.Variable` has:

- :attr:`~cefast.Variable.name`, as written in the file;
- :attr:`~cefast.Variable.value_type`, the CEF type, such as ``'FLOAT'``;
- :attr:`~cefast.Variable.sizes`, the shape of one value, ``()`` when the file gives none;
- :attr:`~cefast.Variable.is_nrv`, true when the values are in the header (``DATA``) instead of
  in each record;
- :attr:`~cefast.Variable.attributes`, a :class:`dict`;
- :attr:`~cefast.Variable.values`, a numpy array.

Shapes
------

A record-varying variable has one row per record. Then come its sizes, unless it is a single
number:

.. code-block:: python

    print(f["density"].values.shape)   # (3,)       no SIZES
    print(f["flux"].values.shape)      # (3, 2, 3)  SIZES = 2, 3

A non-record-varying variable has no record axis. Its shape is its sizes:

.. code-block:: python

    print(f["energy"].is_nrv)          # True
    print(f["energy"].values)          # [  10.  100. 1000.]

For multi-dimensional values, the CEF document says "the last index varies the fastest", which is
numpy's default (C) order. So ``values[r, i, j]`` is what the document calls element ``(i, j)``
of record ``r``.

An ``ISO_TIME_RANGE`` variable gets one more axis of 2 at the end: start, then stop.

Types
-----

=================== =================================== ===================================
CEF ``VALUE_TYPE``  numpy dtype                         Notes
=================== =================================== ===================================
``FLOAT``           ``float32``
``DOUBLE``          ``float64``
``INT``             ``int32``
``BYTE``            ``uint8``                           0 to 255
``ISO_TIME``        ``datetime64[ns]``                  UTC
``ISO_TIME_RANGE``  ``datetime64[ns]``                  last axis: start, stop
``CHAR``            numpy unicode string (``<U…``)
=================== =================================== ===================================

Every number is read exactly as ``strtod`` would read it: correctly rounded to the nearest
``float32`` or ``float64``.

Times keep up to nine digits after the second; more are cut, not rounded. A leap second
(``:60``) is read as the first second of the next minute, because ``datetime64`` has no leap
seconds.

Missing values
--------------

CEFast does not change missing values. Numeric variables keep the ``FILLVAL`` number, as
written in the file:

.. code-block:: python

    print(f["density"].values)                  # [ 3.5e+00 -1.0e+31  3.7e+00]
    print(f["density"].attributes["FILLVAL"])   # -1e+31

Replace them with ``NaN`` before you average or plot: see
:ref:`cookbook:Replace fill values with NaN`.

Time variables are different: a time written exactly as the variable's ``FILLVAL`` becomes
``NaT``, numpy's missing time. A fill date such as the year 9999 would only get in the way:

.. code-block:: python

    print(f["time_tags"].values[2])                  # NaT
    print(f["time_tags"].attributes["FILLVAL"])      # 9999-12-31T23:59:59Z

Attributes
==========

Global attributes are in :attr:`CEF.attributes <cefast.CEF.attributes>`, variable attributes
in :attr:`Variable.attributes <cefast.Variable.attributes>`. Both are plain :class:`dict`, built
again on each access: keep a reference if you read many keys.

.. code-block:: python

    print(f.attributes["OBSERVATORY"])            # Cluster-1
    print(f.attributes["MISSION_TIME_SPAN"])      # (datetime(2000, 7, 16, 0, 0), datetime(2026, 12, 31, 23, 59, 59))
    print(f["density"].attributes["DEPEND_0"])    # time_tags

The value of an attribute follows these rules:

- One entry is the value itself. Several entries are a :class:`list`.
- An entry is text, unless the attribute declares a ``VALUE_TYPE``. Then it has that type: a
  number, a :class:`datetime.datetime` for ``ISO_TIME``, or a pair of them for
  ``ISO_TIME_RANGE``. Text that looks like a number stays text, so version numbers and leading
  zeros keep their exact spelling:

  .. code-block:: python

      print(repr(f["density"].attributes["SIGNIFICANT_DIGITS"]))   # '6'

- ``FILLVAL`` takes the type of its variable when that type is a number, so you can compare it
  with the values directly.

Linking variables
-----------------

Some attributes name another variable of the same file: ``DEPEND_0`` (the time of each
record), ``DEPEND_1`` and up, ``DELTA_PLUS``, ``DELTA_MINUS``, ``LABEL_1``. Look them up in the
file:

.. code-block:: python

    density = f["density"]
    time = f[density.attributes["DEPEND_0"]].values

``DELTA_PLUS`` and ``DELTA_MINUS`` may hold a constant instead of a name. It is text, like any
untyped entry: ``'7.5'``. Check ``name in f`` first, and convert it with :class:`float` when it
is not a variable.

INCLUDE files
=============

``INCLUDE = "file.ceh"`` in a header pastes another header file in place. Archive files use it
for the metadata shared by a whole mission or instrument. CEFast looks for each included file
in this order:

1. ``include_resolver``, when you give one;
2. the directory of the file being read;
3. each directory of ``include_paths``;
4. each directory of the ``CEFPATH`` environment variable;
5. the current directory.

A file that is not found anywhere is an error.

``include_resolver`` is a function that takes the included name and returns its text, or
``None`` to let CEFast search the directories. Use it when the headers do not live on disk:

.. code-block:: python

    headers = {"CL_CH_MISSION.ceh": '''
    START_META = MISSION
       ENTRY = "Cluster"
    END_META = MISSION
    '''}

    content_with_include = content.replace(
        b"START_META = OBSERVATORY", b'INCLUDE = "CL_CH_MISSION.ceh"\nSTART_META = OBSERVATORY')
    g = cefast.load(content_with_include, include_resolver=headers.get)
    print(g.attributes["MISSION"])   # Cluster

``include_paths`` is a list of directories:

.. code-block:: python
    :class: no-check

    f = cefast.load("C1_CP_FGM_SPIN.cef", include_paths=["/data/csa/headers"])

Threads and the GIL
===================

:func:`~cefast.load` releases the GIL while it parses, so other Python threads keep running.
You can load several files from several threads at once: CEFast has no global state.

A large data section (above about 48 kB) is also split across several threads. The result is
the same either way. ``threads`` picks how many:

.. code-block:: python

    f = cefast.load(content, threads=1)   # serial
    f = cefast.load(content, threads=4)   # at most 4 threads
    f = cefast.load(content)              # the default, 0: as many as the machine has

Use ``threads=1`` when you already load many files in parallel, from a thread pool or several
processes.

Memory
======

Numeric and time arrays are not copied: ``.values`` gives a view on the data CEFast parsed. Each
array keeps that data alive by itself, so it stays valid after the :class:`~cefast.CEF` and the
:class:`~cefast.Variable` are gone:

.. code-block:: python

    values = cefast.load(content)["flux"].values
    print(values.sum())   # still valid: 171.0

Every access to ``.values`` returns a view on the same data. Writing into one changes what the
next access returns. Use ``.values.copy()`` when you want your own array.

``CHAR`` arrays are the exception: they are built on each access.

Errors
======

A file that cannot be read or is malformed raises :class:`cefast.ParseError`. The message names
the file, the line and, in the data section, the record:

.. code-block:: python

    broken = content.replace(b"3.7, 13", b"3.7, 13abc")
    try:
        cefast.load(broken)
    except cefast.ParseError as error:
        print(error)   # <memory>:34: invalid DOUBLE value: 13abc (record 3)

``ParseError`` is a :class:`ValueError`, so ``except ValueError`` catches it too.

CEFast never guesses. A record with a missing or extra value, a number followed by garbage, an
empty value, or an unterminated quote is an error. It is never read as shifted columns or as a
partial number. The few things CEFast accepts beyond the CEF 2.0 document are listed in
:doc:`deviations`.
