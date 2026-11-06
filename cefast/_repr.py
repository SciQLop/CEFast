"""Text summaries of a loaded file and its variables, for the REPL and notebooks."""

import datetime

WIDTH = 100


def _value(value):
    if isinstance(value, datetime.datetime):
        return value.isoformat()
    if isinstance(value, tuple):
        return "/".join(_value(part) for part in value)
    if isinstance(value, list):
        return "[" + ", ".join(_value(entry) for entry in value) + "]"
    return repr(value)


def _cut(line):
    return line if len(line) <= WIDTH else line[: WIDTH - 1] + "…"


def _attribute_lines(attributes, indent):
    return [_cut(f"{indent}{name}: {_value(value)}") for name, value in attributes.items()]


def _shape(shape):
    return "(" + ", ".join(map(str, shape)) + ("," if len(shape) == 1 else "") + ")"


def _variable_cells(variable):
    attributes = variable.attributes
    units = attributes.get("UNITS")
    description = attributes.get("CATDESC") or attributes.get("FIELDNAM")
    return [variable.name, variable.value_type,
            _shape(variable.shape) + (" header" if variable.is_nrv else ""),
            units if isinstance(units, str) else "",
            description if isinstance(description, str) else ""]


def _table(rows):
    """Aligned columns, two spaces apart; the last column is free text."""
    widths = [max(len(row[column]) for row in rows) for column in range(len(rows[0]) - 1)]
    return [_cut(("  " + "  ".join(cell.ljust(width) for cell, width in zip(row, widths))
                  + "  " + row[-1]).rstrip())
            for row in rows]


def _records(cef):
    varying = [cef[name].shape[0] for name in cef if not cef[name].is_nrv]
    return varying[0] if varying else None


def cef_repr(cef):
    attributes = cef.attributes
    name = attributes.get("FILE_NAME")
    records = _records(cef)
    counts = f"{len(cef)} variables"
    lines = ["CEF file" + (f" {name}" if isinstance(name, str) else ""),
             (f"{records} records, " if records is not None else "") + counts]
    if len(cef):
        lines += ["", "Variables:"] + _table([_variable_cells(cef[name]) for name in cef])
    if attributes:
        lines += ["", "Attributes:"] + _attribute_lines(attributes, "  ")
    return "\n".join(lines)


def variable_repr(variable):
    kind = "non-record-varying" if variable.is_nrv else "record-varying"
    header = f"{variable.name}: {variable.value_type}, shape {_shape(variable.shape)}, {kind}"
    return "\n".join([_cut(header)] + _attribute_lines(variable.attributes, "  "))
