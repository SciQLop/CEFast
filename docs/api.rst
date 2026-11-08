=============
API reference
=============

.. currentmodule:: cefast

This page lists everything ``cefast`` offers. For explanations and examples, see
:doc:`reading`.

.. autofunction:: load

.. autoclass:: CEF
    :members: keys, values, items, get, attributes
    :special-members: __getitem__

.. autoclass:: Variable
    :members: name, value_type, sizes, shape, is_nrv, attributes, values

.. autoexception:: ParseError

.. autodata:: __version__
    :annotation:

    The version of the package, such as ``'0.1.0'``.
