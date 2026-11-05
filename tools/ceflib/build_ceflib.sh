#!/bin/sh
# Builds IRAP CEFLIB's Python module for the python3 on PATH, entirely under
# ~/.cache/cefast-ceflib (override with CEFLIB_CACHE). Nothing is installed system-wide.
#
# CEFLIB's own makefiles include a site file ($CIS_ROOT/SOFT/GENERIC/Makefile_gcc.default) that is
# not shipped in the tarball, so the two C libraries are compiled here directly. -fPIC because they
# are linked into the Python extension; gnu89/-fcommon because the sources are pre-C99.
set -eu

VERSION=${CEFLIB_VERSION:-1.8.6}
CACHE=${CEFLIB_CACHE:-$HOME/.cache/cefast-ceflib}
SRC=$CACHE/CEFLIB

if [ ! -d "$SRC" ]; then
    mkdir -p "$CACHE"
    curl -sSfL -o "$CACHE/CEFLIB-$VERSION.tar.gz" \
        "https://ceflib.irap.omp.eu/documents/CEFLIB-$VERSION.tar.gz"
    tar xzf "$CACHE/CEFLIB-$VERSION.tar.gz" -C "$CACHE"
fi

CFLAGS="-std=gnu89 -fcommon -fPIC -O2 -Wno-implicit-function-declaration -Wno-int-conversion"
mkdir -p "$SRC/LIB/obj" "$SRC/LIB/bin" "$SRC/C/obj" "$SRC/C/bin"

for src in "$SRC"/LIB/src/*.c; do
    gcc -c $CFLAGS -I "$SRC/LIB/inc" "$src" -o "$SRC/LIB/obj/$(basename "${src%.c}").o"
done
ar rcs "$SRC/LIB/bin/libCIS_gcc.a" "$SRC"/LIB/obj/*.o

for src in "$SRC"/C/src/*.c; do
    gcc -c $CFLAGS -I "$SRC/C/inc" -I "$SRC/LIB/inc" "$src" -o "$SRC/C/obj/$(basename "${src%.c}").o"
done
ar rcs "$SRC/C/bin/libCEF_gcc.a" "$SRC"/C/obj/*.o

PY_INC=$(python3 -c 'import sysconfig; print(sysconfig.get_paths()["include"])')
NUMPY_INC=$(python3 -c 'import numpy; print(numpy.get_include())')
SUFFIX=$(python3 -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX"))')

gcc -shared -fPIC -O2 -std=gnu99 -I "$SRC/C/inc" -I "$SRC/LIB/inc" -I "$PY_INC" -I "$NUMPY_INC" \
    "$SRC/PYTHON/src/ceflib.c" -L "$SRC/LIB/bin" -L "$SRC/C/bin" \
    -lCEF_gcc -lCIS_gcc -lz -lm -o "$SRC/PYTHON/ceflib$SUFFIX"

echo "$SRC/PYTHON"
