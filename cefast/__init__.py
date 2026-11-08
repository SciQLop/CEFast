"""CEFast: a fast, maintained reader for ESA's Cluster Exchange Format (CEF 2.0)."""

from collections.abc import Mapping

from ._cefast import CEF, ParseError, Variable, load
from ._repr import cef_repr, variable_repr
from ._version import __version__

CEF.__repr__ = cef_repr
Variable.__repr__ = variable_repr
del cef_repr, variable_repr

# The binding gives the core (__getitem__, __iter__, __len__, __contains__, keys); Mapping's
# own methods build the rest on top of it, with the exact standard semantics.
CEF.values = Mapping.values
CEF.items = Mapping.items
CEF.get = Mapping.get
Mapping.register(CEF)

__all__ = ["CEF", "ParseError", "Variable", "__version__", "load"]
