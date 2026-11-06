==========
Quickstart
==========

This page takes five minutes. You will download a real CEF file, read it and plot it. Every
example on this page runs as is: copy it into Python and try it.

Get a file
==========

We use two hours of magnetic field data from the Cluster 1 spacecraft, at spin resolution
(4 s). The file is 170 kB. The Cluster Science Archive delivers files in a tar archive, so this
small function downloads one and returns its name and content:

.. code-block:: python

    import io
    import tarfile
    import urllib.request

    def fetch_csa(dataset, start, stop):
        """Download one CEF file from the Cluster Science Archive."""
        url = ("https://csa.esac.esa.int/csa-sl-tap/data?RETRIEVAL_TYPE=product"
               f"&DATASET_ID={dataset}&START_DATE={start}&END_DATE={stop}"
               "&DELIVERY_FORMAT=CEF&DELIVERY_INTERVAL=all")
        with urllib.request.urlopen(url) as response:
            archive = tarfile.open(fileobj=io.BytesIO(response.read()))
        member = next(m for m in archive if m.name.endswith(".cef"))
        return member.name.rsplit("/", 1)[-1], archive.extractfile(member).read()

    name, content = fetch_csa("C1_CP_FGM_SPIN",
                              "2005-06-10T00:00:00Z", "2005-06-10T02:00:00Z")
    print(name)   # C1_CP_FGM_SPIN__20050610_000000_20050610_020000_V140305.cef

Open it
=======

:func:`cefast.load` takes a path, or the content of a file as bytes. We already have the bytes,
so there is nothing to save:

.. code-block:: python

    import cefast

    f = cefast.load(content)
    print(f)   # <cefast.CEF with 7 variables>

To read a file from disk, give its path instead. Gzipped files (``.cef.gz``) work the same way:

.. code-block:: python

    with open(name, "wb") as out:
        out.write(content)

    f = cefast.load(name)

See what's inside
=================

A CEF file holds **variables**. You use it like a dictionary of variables, in file order:

.. code-block:: python

    for variable_name in f:
        print(f[variable_name])

.. code-block:: text

    <cefast.Variable 'time_tags__C1_CP_FGM_SPIN' ISO_TIME record-varying>
    <cefast.Variable 'half_interval__C1_CP_FGM_SPIN' FLOAT record-varying>
    <cefast.Variable 'B_vec_xyz_gse__C1_CP_FGM_SPIN' FLOAT record-varying>
    <cefast.Variable 'B_mag__C1_CP_FGM_SPIN' FLOAT record-varying>
    <cefast.Variable 'sc_pos_xyz_gse__C1_CP_FGM_SPIN' FLOAT record-varying>
    <cefast.Variable 'range__C1_CP_FGM_SPIN' INT record-varying>
    <cefast.Variable 'tm__C1_CP_FGM_SPIN' INT record-varying>

Archive variable names end with ``__`` and the dataset name. CEFast keeps them as written.

Get the data
============

``.values`` gives a numpy array. The magnetic field has one row per record and three columns:

.. code-block:: python

    B = f["B_vec_xyz_gse__C1_CP_FGM_SPIN"]
    print(B.values.shape, B.values.dtype)   # (1730, 3) float32

Times are numpy ``datetime64[ns]``:

.. code-block:: python

    time = f["time_tags__C1_CP_FGM_SPIN"].values
    print(time[0])   # 2005-06-10T00:00:01.855000000

Read the metadata
=================

Each variable has attributes: units, a description, a fill value, the name of its time
variable, and so on.

.. code-block:: python

    print(B.attributes["UNITS"])      # nT
    print(B.attributes["CATDESC"])    # Magnetic Field Vector, spin resolution in GSE
    print(B.attributes["LABEL_1"])    # ['Bx', 'By', 'Bz']
    print(B.attributes["DEPEND_0"])   # time_tags__C1_CP_FGM_SPIN

The file has global attributes too, about the mission, the instrument and the dataset:

.. code-block:: python

    print(f.attributes["DATASET_TITLE"])    # Magnetic field, spin resolution
    print(f.attributes["FILE_TIME_SPAN"])   # (datetime(2005, 6, 10, 0, 0), datetime(2005, 6, 10, 2, 0))

Plot it
=======

.. code-block:: python

    import matplotlib.pyplot as plt

    time = f[B.attributes["DEPEND_0"]].values
    plt.plot(time, B.values, label=B.attributes["LABEL_1"])
    plt.ylabel(f"B GSE ({B.attributes['UNITS']})")
    plt.legend()
    plt.show()

This file has no missing data. When a file does, the missing values hold the ``FILLVAL``
number, often ``-1e30``: replace them with ``NaN`` before you plot or average. See
:ref:`cookbook:Replace fill values with NaN`.

Next steps
==========

- :doc:`reading` explains everything about loading files, shapes, types and attributes.
- :doc:`cookbook` has recipes for xarray, pandas and several files.
- :doc:`cef` shows what a CEF file looks like inside.
