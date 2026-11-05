<p align="center"><img src="docs/images/cefast_logo.png" alt="CEFast" width="600"></p>

# CEFast

A fast, maintained reader for [CEF](https://www.cosmos.esa.int/web/csa/software) (Cluster Exchange
Format) files, the text format of ESA's Cluster Science Archive. Header-only C++20 core with
Python bindings (`cefast` on PyPI).

**Status:** in development, not released yet. Design: `docs/superpowers/specs/2026-11-05-cefast-design.md`.

```python
import cefast
f = cefast.load("C1_CP_FGM_SPIN__20010201_000000_20010202_000000_V140305.cef.gz")
f["B_vec_xyz_gse__C1_CP_FGM_SPIN"].values   # numpy array
```
