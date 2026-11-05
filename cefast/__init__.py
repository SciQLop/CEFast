"""CEFast: a fast, maintained reader for ESA's Cluster Exchange Format (CEF 2.0)."""

from ._cefast import CEF, ParseError, Variable, load

__all__ = ["CEF", "ParseError", "Variable", "load"]
