#!/usr/bin/env python
# Sphinx configuration for the CEFast documentation.
import re
from importlib import metadata
from pathlib import Path

extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.intersphinx',
    'sphinx.ext.autosectionlabel',
    'sphinx_design',
    'sphinx_copybutton',
    'numpydoc',
    'myst_parser',
]

autosectionlabel_prefix_document = True
numpydoc_show_class_members = False
numpydoc_class_members_toctree = False
autodoc_member_order = 'bysource'
# The Parameters sections give the types; pybind11's annotations (SupportsInt, ...) only add noise.
autodoc_typehints = 'none'

source_suffix = {'.rst': 'restructuredtext', '.md': 'markdown'}
master_doc = 'index'
# Only the pages of the site: the design notes and perf logs next to them are not part of it.
exclude_patterns = ['_build', 'superpowers', 'perf', '*.md', 'Thumbs.db', '.DS_Store']

project = 'CEFast'
copyright = '2026, Alexis Jeandet'
author = 'Alexis Jeandet'


def _version() -> str:
    """The installed package's version, else the one meson.build declares (docs built from a checkout)."""
    try:
        return metadata.version('cefast')
    except metadata.PackageNotFoundError:
        meson = (Path(__file__).parent.parent / 'meson.build').read_text()
        return re.search(r"^\s*version\s*:\s*'([^']+)'", meson, re.MULTILINE).group(1)


version = release = _version()

language = 'en'

html_theme = 'furo'
html_title = f'CEFast {version}'
html_logo = 'images/cefast_logo.png'
html_static_path = ['_static']
# A Run button on every Python example: Pyodide, loaded on the first click (see playground.js).
html_css_files = ['playground.css']
html_js_files = ['playground.js']
html_theme_options = {
    'source_repository': 'https://github.com/SciQLop/CEFast/',
    'source_branch': 'main',
    'source_directory': 'docs/',
}

intersphinx_mapping = {
    'python': ('https://docs.python.org/3', None),
    'numpy': ('https://numpy.org/doc/stable/', None),
    'matplotlib': ('https://matplotlib.org/stable/', None),
    'xarray': ('https://docs.xarray.dev/en/stable/', None),
    'pandas': ('https://pandas.pydata.org/docs/', None),
}
