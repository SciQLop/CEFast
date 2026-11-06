======
CEFast
======

**Read CEF files, fast, into numpy.**

CEF (Cluster Exchange Format) is the text format of ESA's
`Cluster Science Archive <https://csa.esac.esa.int/>`_ (CSA), which delivers the data of the
four Cluster spacecraft and of Double Star in it. CEFast reads CEF 2.0 files into numpy arrays,
from Python or from C++.

.. code-block:: python
    :class: no-check

    import cefast

    f = cefast.load("C1_CP_FGM_SPIN__20050610_000000_20050610_020000_V140305.cef")
    f["B_vec_xyz_gse__C1_CP_FGM_SPIN"].values   # numpy float32, shape (records, 3)
    f["time_tags__C1_CP_FGM_SPIN"].values       # numpy datetime64[ns]

.. code-block:: console

    $ pip install cefast

What do you want to do?
=======================

.. grid:: 1 1 2 2
    :gutter: 3

    .. grid-item-card:: 🔭 Read Cluster data
        :link: quickstart
        :link-type: doc

        You have CEF files from the Cluster Science Archive and want the data in numpy,
        xarray or pandas.

        Start with the :doc:`quickstart`, then :doc:`reading`.

    .. grid-item-card:: 📄 Understand the format
        :link: cef
        :link-type: doc

        You want to know what is inside a CEF file: the header, the variables, the data
        records.

        Read :doc:`cef`.

    .. grid-item-card:: ⚙️ Use CEFast from C++
        :link: cpp
        :link-type: doc

        CEFast is a C++20 library made only of headers, with no global state.

        Go to :doc:`cpp`.

    .. grid-item-card:: 🔁 Coming from CEFLIB
        :link: faq
        :link-type: doc

        You used CEFLIB, IRAP's CEF reader, and want to know what changes.

        See :ref:`faq:Coming from CEFLIB`.

Why CEFast?
===========

- **Easy to install.** ``pip install cefast`` ships ready-made packages for Linux, macOS and
  Windows, Intel and ARM, and for Pyodide. No compiler needed.
- **Fast.** About 3× faster than CEFLIB on the CSA sample corpus, and faster on every file.
  Large files are parsed on several threads. See :doc:`performance`.
- **Correct.** Every number is correctly rounded. A malformed file raises an error that names
  the file, the line and the record: it never returns shifted columns. See :doc:`deviations`
  for what CEFast accepts beyond the CEF 2.0 document.
- **Thread-safe.** No global state: load as many files as you like, from as many threads as
  you like. Loading releases the GIL.
- **Permissive license.** MIT.

.. toctree::
   :hidden:
   :caption: Getting started

   installation
   quickstart
   cef

.. toctree::
   :hidden:
   :caption: Reading data

   reading
   cookbook

.. toctree::
   :hidden:
   :caption: C++

   cpp

.. toctree::
   :hidden:
   :caption: Reference

   api
   performance
   deviations
   faq
   history
   contributing
