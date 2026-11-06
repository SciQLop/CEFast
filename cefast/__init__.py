"""CEFast: a fast, maintained reader for ESA's Cluster Exchange Format (CEF 2.0)."""

from ._cefast import CEF, ParseError, Variable, load
from ._repr import cef_repr, variable_repr
from ._version import __version__

CEF.__repr__ = cef_repr
Variable.__repr__ = variable_repr
del cef_repr, variable_repr

__all__ = ["CEF", "ParseError", "Variable", "__version__", "load"]
