#!/usr/bin/env python3
"""Write the libFuzzer seed corpus for tools/fuzz.

The CSA files are not in the repository, so the corpus is generated: every real header (first
4 KiB) and every real file seed the load and lexer targets, and the grammar samples seed the
time and float targets. Run with no argument for the default location.
"""

from __future__ import annotations

import argparse
import gzip
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ROOT / "tests" / "resources"
HEADER_SEED_BYTES = 4096

TIME_SEEDS = [
    "2000-01-01T00:00:00Z",
    "2000-01-01T00:00:00.000Z",
    "2000-01-01T00:00:00.000000000Z",
    "2000-01-01T00:00:00.1Z",
    "2000-01-01t00:00:00z",
    "2000-02-29T23:59:60Z",
    "1900-01-01T00:00:00Z",
    "9999-12-31T23:59:59.999999999Z",
    "2000-01-01T00:00:00Z/2000-01-01T00:00:01Z",
    "9999-99-99T99:99:99Z",
    "2000-01-01T00:00:00",
    "2000-13-01T00:00:00Z",
]

FLOAT_SEEDS = [
    "0",
    "-0",
    "+0",
    "1",
    "-1",
    "+1",
    "1.5",
    "+1.5",
    "-1.5",
    "0.00239",
    "15462.2",
    "1e3",
    "1E3",
    "1e+3",
    "1e-3",
    "+1e3",
    "1.",
    ".5",
    "5e-324",
    "1.7976931348623157e308",
    "1e-310",
    "1e-46",
    "3.5e38",
    "inf",
    "-inf",
    "+inf",
    "nan",
    "0x10",
    "1,5",
    " 1",
    "1 ",
    "1e",
    "+",
    "-",
    "",
]

LEXER_SEEDS = [
    'FILE_FORMAT_VERSION = "CEF-2.0"\nDATA_UNTIL = END_DATA\n',
    'FILE_FORMAT_VERSION = "CEF-2.0"\nSTART_VARIABLE = v\n  VALUE_TYPE = FLOAT\n'
    "  SIZES = 2\nEND_VARIABLE = v\nDATA_UNTIL = END_DATA\n1, 2\n",
    'FILE_FORMAT_VERSION = "CEF-2.0"\nSTART_META = M\n  ENTRY = X\n'
    "  VALUE_TYPE = INT\n  DATA = 1\nEND_META = M\nDATA_UNTIL = END_DATA\n",
    'FILE_FORMAT_VERSION = "CEF-2.0"\nINCLUDE = "a.ceh"\nDATA_UNTIL = END_DATA\n',
    'FILE_FORMAT_VERSION = "CEF-2.0"\nINCLUDE = "b.ceh"\nDATA_UNTIL = END_DATA\n',
]


def write(directory: Path, name: str, data: bytes) -> None:
    (directory / name).write_bytes(data)


def number_tokens(text: str) -> list[str]:
    return [
        token for token in re.split(r"[,;\s\n]+", text) if token and len(token) < 32
    ]


def seed_load_and_lexer(load_dir: Path, lexer_dirs: list[Path]) -> None:
    for path in sorted((RESOURCES / "csa").glob("*.cef")):
        data = path.read_bytes()
        write(load_dir, path.name, data)
        write(load_dir, path.name + ".head", data[:HEADER_SEED_BYTES])
        for lexer_dir in lexer_dirs:
            write(lexer_dir, path.name + ".head", data[:HEADER_SEED_BYTES])
    for path in sorted((RESOURCES / "fixtures").rglob("*")):
        if not path.is_file():
            continue
        data = path.read_bytes()
        name = "__".join(path.relative_to(RESOURCES / "fixtures").parts)
        write(load_dir, name, data)
        if path.suffix == ".gz":
            try:
                write(load_dir, name + ".plain", gzip.decompress(data))
            except (OSError, EOFError):
                pass  # truncated.cef.gz is a seed as-is, not a decompressible one
        elif b"DATA_UNTIL" in data:
            for lexer_dir in lexer_dirs:
                write(lexer_dir, name, data)


def seed_time(time_dir: Path) -> None:
    for index, seed in enumerate(TIME_SEEDS):
        write(time_dir, f"time{index}", seed.encode())
    for path in sorted((RESOURCES / "fixtures").glob("*.cef")):
        text = path.read_text(errors="replace")[:HEADER_SEED_BYTES]
        for index, token in enumerate(re.findall(r"\d{4}-\d{2}-\d{2}T[^ ,\n]+", text)):
            write(time_dir, f"{path.stem}-{index}", token.encode())


def seed_float(float_dir: Path) -> None:
    for index, seed in enumerate(FLOAT_SEEDS):
        write(float_dir, f"float{index}", seed.encode())
    for path in sorted((RESOURCES / "fixtures").glob("*.cef")):
        tokens = number_tokens(path.read_text(errors="replace")[:HEADER_SEED_BYTES])
        for index, token in enumerate(tokens[:200]):
            write(float_dir, f"{path.stem}-{index}", token.encode())


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output", type=Path, default=Path.home() / ".cache/cefast-fuzz/corpus"
    )
    arguments = parser.parse_args()
    if not (RESOURCES / "csa").is_dir() or not any((RESOURCES / "csa").glob("*.cef")):
        raise SystemExit(
            "the CSA corpus is missing: run python -I tools/fetch_csa_corpus.py"
        )
    for name in ("load", "time", "float", "lexer", "lexer_diff"):
        directory = arguments.output / name
        if directory.exists():
            shutil.rmtree(directory)
        directory.mkdir(parents=True)
    # The differential target takes exactly the lexer seeds: both sides read the same input.
    lexer_dirs = [arguments.output / "lexer", arguments.output / "lexer_diff"]
    seed_load_and_lexer(arguments.output / "load", lexer_dirs)
    for index, seed in enumerate(LEXER_SEEDS):
        for lexer_dir in lexer_dirs:
            write(lexer_dir, f"lexer{index}", seed.encode())
    seed_time(arguments.output / "time")
    seed_float(arguments.output / "float")
    for name in ("load", "time", "float", "lexer", "lexer_diff"):
        count = len(list((arguments.output / name).iterdir()))
        print(f"{name}: {count} seeds in {arguments.output / name}")


if __name__ == "__main__":
    main()
