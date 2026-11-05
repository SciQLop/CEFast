#!/usr/bin/env python3
"""Fetch the CSA test corpus described by tests/resources/csa_manifest.json.

The manifest pins, for each corpus file, the dataset, the requested interval, the file name
prefix (the CSA version suffix may change between downloads) and a *normalised* SHA-256.

CSA rewrites the header on every download: the `!---- CSA MERGED CEF FILE (…) ----` banner and
the GENERATION_DATE metadata block carry the download time, and the `!RECORDS=` comment is
regenerated too. Normalisation drops every `!` comment line and the whole GENERATION_DATE block,
and nothing else — two downloads of the same interval then hash identically.

Usage:
    python -I tools/fetch_csa_corpus.py                      # fetch into tests/resources/csa
    python -I tools/fetch_csa_corpus.py --dest /tmp/corpus   # fetch somewhere else
    python -I tools/fetch_csa_corpus.py --write-manifest     # rebuild the manifest from --dest

Standard library only (urllib, tarfile, hashlib, json). Exits non-zero, naming the dataset, on
an HTTP error, a missing file, or a hash mismatch. Requires Python 3.12 for tarfile's data filter
(older versions fall back to the legacy filter, the members are read from a private temp dir).
"""

import argparse
import hashlib
import io
import json
import re
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DEST = REPO_ROOT / "tests" / "resources" / "csa"
DEFAULT_MANIFEST = REPO_ROOT / "tests" / "resources" / "csa_manifest.json"

URL = "https://csa.esac.esa.int/csa-sl-tap/data"
USER_AGENT = "cefast-corpus-fetch/1.0 (+https://github.com/SciQLop/CEFast)"
ATTEMPTS = 3
BACKOFF_SECONDS = 2.0

NAME_RE = re.compile(
    r"^(?P<dataset>.+?)__(?P<start>\d{8}_\d{6})_(?P<stop>\d{8}_\d{6})_V\d+\.cef$"
)


class FetchError(RuntimeError):
    pass


def iso(stamp):
    """20020202_003000 -> 2002-02-02T00:30:00Z"""
    return (
        f"{stamp[0:4]}-{stamp[4:6]}-{stamp[6:8]}T"
        f"{stamp[9:11]}:{stamp[11:13]}:{stamp[13:15]}Z"
    )


def header_key(line):
    """KEY of a `KEY = value` header line, upper-cased, or None."""
    head = line.split(b"=", 1)[0].strip()
    if head == line.strip():
        return None
    return head.upper()


def header_value(line):
    return line.split(b"=", 1)[1].strip().upper()


def normalized(raw):
    """Bytes with everything CSA regenerates at download time removed."""
    kept = []
    in_generation_date = False
    for line in raw.splitlines(keepends=True):
        if line.lstrip().startswith(b"!"):
            continue
        key = header_key(line)
        if key == b"START_META":
            in_generation_date = header_value(line) == b"GENERATION_DATE"
            continue
        if in_generation_date:
            if key == b"END_META":
                in_generation_date = False
            continue
        kept.append(line)
    return b"".join(kept)


def normalized_sha256(raw):
    return hashlib.sha256(normalized(raw)).hexdigest()


def entry_for(path):
    match = NAME_RE.match(path.name)
    if match is None:
        raise FetchError(f"{path.name}: not a CSA CEF file name")
    start, stop = iso(match["start"]), iso(match["stop"])
    return {
        "dataset_id": match["dataset"],
        "start": start,
        "stop": stop,
        "file_name_prefix": f"{match['dataset']}__{match['start']}_{match['stop']}_",
        "normalized_sha256": normalized_sha256(path.read_bytes()),
        "bytes": path.stat().st_size,
    }


def write_manifest(dest, manifest_path):
    entries = [entry_for(path) for path in sorted(dest.glob("*.cef"))]
    if not entries:
        raise FetchError(f"{dest}: no .cef file to describe")
    manifest_path.write_text(
        json.dumps(
            {"files": sorted(entries, key=lambda e: (e["dataset_id"], e["start"]))},
            indent=2,
        )
        + "\n"
    )
    print(f"wrote {manifest_path} ({len(entries)} files)")


def request_url(entry):
    query = urllib.parse.urlencode(
        {
            "RETRIEVAL_TYPE": "product",
            "DATASET_ID": entry["dataset_id"],
            "START_DATE": entry["start"],
            "END_DATE": entry["stop"],
            "DELIVERY_FORMAT": "CEF",
            "DELIVERY_INTERVAL": "all",
        }
    )
    return f"{URL}?{query}"


def download(entry):
    request = urllib.request.Request(
        request_url(entry), headers={"User-Agent": USER_AGENT}
    )
    with urllib.request.urlopen(request, timeout=180) as response:
        return response.read()


def cef_members(payload):
    """(name, bytes) of every .cef in the delivered tar, extracted to a private temp dir."""
    try:
        archive = tarfile.open(fileobj=io.BytesIO(payload), mode="r:*")
    except tarfile.TarError as error:
        raise FetchError(f"not a tar archive ({error})") from error
    with archive, tempfile.TemporaryDirectory() as workdir:
        members = [
            m for m in archive.getmembers() if m.isfile() and m.name.endswith(".cef")
        ]
        if not members:
            raise FetchError("no .cef file in the delivered archive")
        try:
            archive.extractall(workdir, members=members, filter="data")
        except TypeError:  # Python < 3.12 has no extraction filters
            archive.extractall(workdir, members=members)
        return [(m.name, (Path(workdir) / m.name).read_bytes()) for m in members]


def with_retries(entry, action):
    for attempt in range(1, ATTEMPTS + 1):
        try:
            return action()
        except (urllib.error.URLError, OSError, FetchError) as error:
            if attempt == ATTEMPTS:
                raise FetchError(f"{entry['dataset_id']}: {error}") from error
            pause = BACKOFF_SECONDS * 2 ** (attempt - 1)
            print(
                f"  {entry['dataset_id']}: {error}; retrying in {pause:.0f}s",
                file=sys.stderr,
            )
            time.sleep(pause)
    raise AssertionError("unreachable")


def existing_file(dest, entry):
    matches = sorted(dest.glob(f"{entry['file_name_prefix']}*.cef"))
    return matches[0] if matches else None


def store(dest, name, payload, entries_by_prefix):
    """Write one delivered .cef if a manifest entry covers it, and check its hash."""
    base = Path(name).name
    entry = next(
        (e for prefix, e in entries_by_prefix.items() if base.startswith(prefix)), None
    )
    if entry is None:
        print(f"  ignoring {base} (not in the manifest)", file=sys.stderr)
        return None
    digest = normalized_sha256(payload)
    if digest != entry["normalized_sha256"]:
        raise FetchError(
            f"{entry['dataset_id']}: hash mismatch for {base} "
            f"(expected {entry['normalized_sha256']}, got {digest})"
        )
    target = dest / base
    staging = target.with_suffix(".cef.part")
    staging.write_bytes(payload)
    staging.replace(target)
    print(f"  {base} ({len(payload)} bytes, hash ok)")
    return base


def fetch(dest, manifest_path, delay):
    manifest = json.loads(manifest_path.read_text())
    entries = manifest["files"]
    by_prefix = {entry["file_name_prefix"]: entry for entry in entries}
    dest.mkdir(parents=True, exist_ok=True)

    fetched = 0
    for index, entry in enumerate(entries, start=1):
        label = f"[{index}/{len(entries)}] {entry['dataset_id']}"
        present = existing_file(dest, entry)
        if (
            present is not None
            and normalized_sha256(present.read_bytes()) == entry["normalized_sha256"]
        ):
            print(f"{label}: up to date")
            continue
        if delay and index > 1:
            time.sleep(delay)
        print(f"{label}: downloading {entry['start']} .. {entry['stop']}")
        members = with_retries(entry, lambda: cef_members(download(entry)))
        stored = [store(dest, name, payload, by_prefix) for name, payload in members]
        if not any(
            name and name.startswith(entry["file_name_prefix"]) for name in stored
        ):
            raise FetchError(
                f"{entry['dataset_id']}: archive has no file named "
                f"{entry['file_name_prefix']}*.cef (got {[Path(n).name for n, _ in members]})"
            )
        fetched += 1
    print(
        f"done: {len(entries)} files, {fetched} downloaded, {len(entries) - fetched} up to date"
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dest", type=Path, default=DEFAULT_DEST)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument(
        "--write-manifest",
        action="store_true",
        help="describe the .cef files already in --dest and write the manifest",
    )
    parser.add_argument(
        "--delay",
        type=float,
        default=1.0,
        help="seconds between downloads (polite sequential requests, default 1)",
    )
    arguments = parser.parse_args(argv)
    try:
        if arguments.write_manifest:
            write_manifest(arguments.dest, arguments.manifest)
        else:
            fetch(arguments.dest, arguments.manifest, arguments.delay)
    except FetchError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
