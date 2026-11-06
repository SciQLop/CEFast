============
Installation
============

Python
======

Install ``cefast`` with pip:

.. code-block:: console

    $ python -m pip install cefast

Ready-made packages (wheels) exist for:

- Python 3.10 and newer, including free-threaded builds.
- Linux (x86_64 and ARM64), macOS 13 and newer (Intel and Apple Silicon), and Windows (x86_64
  and ARM64). Windows on ARM starts at Python 3.11.
- Pyodide, so CEFast also runs in the browser, in JupyterLite for example.

You don't need a compiler. The only runtime dependency is ``numpy``.

Check that it works:

.. code-block:: python

    import cefast
    print(cefast.__version__)

From source
-----------

On other platforms, pip builds CEFast from the source package. That needs a C++20 compiler.
pip fetches meson and ninja by itself.

To build the latest development version:

.. code-block:: console

    $ python -m pip install git+https://github.com/SciQLop/CEFast

C++
===

CEFast is a library made only of headers. With meson, add it as a subproject: see
:doc:`cpp`.
