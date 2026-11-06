===================
CEF in five minutes
===================

A CEF file is plain text. It has two parts:

1. A **header**, which describes the file and its variables.
2. A **data section**, with one line (or more) per record.

The format is defined by ESA's document *CEF: Cluster Exchange Format* (DS-QMW-TN-0010),
version 2.0. CEFast reads that version. Version 1 files are refused: the archive never served
them, and version 2 is not backward compatible with them.

A complete file
===============

Here is a small but complete CEF file. It has three variables: a time, a magnetic field vector,
and a list of labels.

.. code-block:: python

    content = b'''\
    FILE_NAME = "C1_CP_DEMO__20050610_000000_20050610_000012_V01.cef"
    FILE_FORMAT_VERSION = "CEF-2.0"
    END_OF_RECORD_MARKER = "$"

    START_META = DATASET_TITLE
       ENTRY = "Magnetic field, demo"
    END_META = DATASET_TITLE

    START_VARIABLE = time_tags
      VALUE_TYPE = ISO_TIME
      UNITS = "s"
    END_VARIABLE = time_tags

    START_VARIABLE = B_vec_xyz_gse
      SIZES = 3
      VALUE_TYPE = FLOAT
      UNITS = "nT"
      FILLVAL = -1.0E31
      DEPEND_0 = time_tags
    END_VARIABLE = B_vec_xyz_gse

    START_VARIABLE = label
      VALUE_TYPE = CHAR
      SIZES = 3
      DATA = "Bx", "By", "Bz"
    END_VARIABLE = label

    DATA_UNTIL = "END_OF_DATA"
    2005-06-10T00:00:00.000Z, 13.50, 219.42, 71.04 $
    2005-06-10T00:00:04.000Z, 13.66, 219.35, 71.30 $
    2005-06-10T00:00:08.000Z, -1.0E31, -1.0E31, -1.0E31 $
    END_OF_DATA
    '''

The header
==========

The header is a list of ``KEYWORD = value`` lines, grouped in blocks.

- ``FILE_NAME`` and ``FILE_FORMAT_VERSION`` say what the file is.
- ``END_OF_RECORD_MARKER`` is the character that ends each record, here ``$``. Without it, a
  record ends at the end of its line.
- A ``START_META`` … ``END_META`` block is a **global attribute**. Its ``ENTRY`` lines are its
  values. The archive's files carry many of them: mission, spacecraft, instrument, dataset,
  caveats.
- A ``START_VARIABLE`` … ``END_VARIABLE`` block declares a **variable**. Its lines are the
  variable's attributes.
- ``INCLUDE = "file.ceh"`` pastes another header file in place. Archive files use it for the
  parts shared by every file of a mission. See :ref:`reading:INCLUDE files`.
- ``DATA_UNTIL`` ends the header. Its value is the line that ends the data section. When it is
  ``EOF``, the data runs to the end of the file.

Three variable attributes shape the data:

- ``VALUE_TYPE``: ``FLOAT``, ``DOUBLE``, ``INT``, ``BYTE``, ``CHAR``, ``ISO_TIME`` or
  ``ISO_TIME_RANGE``.
- ``SIZES``: the shape of one value, ``3`` for a vector, ``16, 32`` for a 2-D table. Without
  it, a value is a single number.
- ``DATA``: when present, the variable's values are right here in the header. The variable is
  then **non-record-varying**: it has the same value for the whole file, like the labels above.

The data section
================

Each record holds one value of every record-varying variable, in the order the header declares
them, separated by commas. Here a record is a time, then the three components of the field:
four values.

Missing values are written as the variable's ``FILLVAL``, like the third record above.

Times are ISO 8601 text, in UTC: ``2005-06-10T00:00:04.000Z``.

Reading it
==========

.. code-block:: python

    import cefast

    f = cefast.load(content)
    print(list(f))                         # ['time_tags', 'B_vec_xyz_gse', 'label']
    print(f["B_vec_xyz_gse"].values)       # 3 records of 3 floats, the last one -1e31
    print(f["label"].values)               # ['Bx' 'By' 'Bz']
    print(f["label"].is_nrv)               # True
    print(f.attributes["DATASET_TITLE"])   # Magnetic field, demo

The structural keywords (``END_OF_RECORD_MARKER``, ``DATA_UNTIL``, ``INCLUDE``) are used to read
the file. They are not in :attr:`~cefast.CEF.attributes`. Neither are ``VALUE_TYPE``,
``SIZES`` and ``DATA`` in :attr:`~cefast.Variable.attributes`: they are the variable's
:attr:`~cefast.Variable.value_type`, :attr:`~cefast.Variable.sizes` and
:attr:`~cefast.Variable.values`.
