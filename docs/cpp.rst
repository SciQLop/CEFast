=========
C++ guide
=========

CEFast is a C++20 library made only of headers. It needs zlib, and two header libraries that
meson fetches for you: `fast_float <https://github.com/fastfloat/fast_float>`_ and
`cpp_utils <https://github.com/jeandet/cpp_utils>`_. It has no global state: load files from
as many threads as you like.

Add it to a meson project
=========================

Put this wrap file in your ``subprojects/`` directory, as ``subprojects/cefast.wrap``:

.. code-block:: ini

    [wrap-git]
    url = https://github.com/SciQLop/CEFast.git
    revision = v0.1.1
    depth = 1

    [provide]
    cefast = cefast_dep

Then ask for the dependency. The two options skip CEFast's own tests and Python module:

.. code-block:: meson

    cefast_dep = dependency('cefast',
        default_options : ['python=false', 'with_tests=false'])
    executable('demo', 'main.cpp', dependencies : cefast_dep)

Meson finds CEFast's own dependencies by itself. It uses the system zlib when there is one.

Load a file
===========

.. code-block:: cpp

    #include <cefast/cefast.hpp>

    #include <iostream>
    #include <variant>

    int main(int argc, char** argv)
    {
        try
        {
            const cefast::cef_file file = cefast::load(argv[1]);
            for (const cefast::variable& var : file.variables)
            {
                std::cout << var.name << ": " << (var.is_nrv ? "header" : "records") << ' '
                          << var.records << '\n';
                if (const auto* values
                    = std::get_if<cpp_utils::containers::no_init_vector<float>>(&var.values))
                    if (!values->empty())
                        std::cout << "  first value: " << values->front() << '\n';
            }
        }
        catch (const cefast::parse_error& error)
        {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }

``cefast::load`` has two forms:

- ``load(const std::filesystem::path&, const load_options& = {})`` reads a file. A gzipped
  file is recognised from its content.
- ``load(std::span<const char>, const load_options& = {})`` reads a file already in memory.

Both return a ``cefast::cef_file``, or throw ``cefast::parse_error``.

The data model
==============

Everything is in ``<cefast/model.hpp>``. These are plain structs, with no hidden state.

.. code-block:: cpp

    struct cef_file
    {
        attributes globals;              // the global attributes
        std::vector<variable> variables; // in file order
    };

    struct variable
    {
        std::string name;
        value_type type;                 // FLOAT, DOUBLE, INT, BYTE, ISO_TIME, ISO_TIME_RANGE, CHAR
        std::vector<std::size_t> sizes;  // the SIZES attribute, empty when there is none
        bool is_nrv;                     // values come from the header (DATA)
        std::size_t records;             // number of records
        column values;                   // all the values, flat
        attributes attrs;                // the other attributes
        std::string fillval_token;       // FILLVAL as written in the file
    };

``values`` is a ``std::variant`` with one vector type per value type. All values of all records
are in one flat vector, in file order: record after record, and inside a record the last size
varies fastest.

=================== ===================================================
``value_type``      element type in ``values``
=================== ===================================================
``FLOAT``           ``float``
``DOUBLE``          ``double``
``INT``             ``std::int32_t``
``BYTE``            ``std::uint8_t``
``ISO_TIME``        ``std::int64_t``, nanoseconds since 1970-01-01 UTC
``ISO_TIME_RANGE``  ``std::int64_t``, start and stop one after the other
``CHAR``            ``std::string``
=================== ===================================================

A time written as its variable's ``FILLVAL`` becomes ``std::numeric_limits<std::int64_t>::min()``,
which numpy reads as ``NaT``. Numeric fill values are left as they are.

The numeric vectors are ``cpp_utils::containers::no_init_vector``: a ``std::vector`` whose
allocator does not zero the memory first. Use it like a ``std::vector``.

An attribute is a name and a list of entries. Each entry is a ``std::variant`` of
``std::string``, ``std::int64_t``, ``double``, ``iso_time`` and ``iso_time_range``. Entries
are text unless the attribute declares a ``VALUE_TYPE``: see :ref:`reading:Attributes`.

Options
=======

.. code-block:: cpp

    cefast::load_options options;
    options.include_paths = { "/data/csa/headers" };
    options.include_resolver = [](std::string_view name) -> std::optional<std::string>
    {
        return std::nullopt;   // the text of the included file, or nullopt to search on disk
    };
    options.threads = 1;       // 0: from the machine (default), 1: serial, N: at most N
    auto file = cefast::load("file.cef", options);

``INCLUDE`` files are searched in the order given in :ref:`reading:INCLUDE files`.

With ``threads`` other than 1, a large data section is parsed by a pool of worker threads
shared by the whole process. The pool starts on first use and is never stopped.

Errors
======

``cefast::parse_error`` derives from ``std::runtime_error``. Its ``what()`` names the file, the
line and, in the data section, the record:

.. code-block:: text

    broken.cef:214: expected 31 values, got 30 (record 12)

A file that cannot be read throws ``parse_error`` too, with line 0.
