# The extension is declared in cefast/meson.build (next to the package's __init__.py, so the build
# tree has the same `cefast/` layout as the installed package); this file only carries its
# dependency and compiler flags.
pybind11_dep = dependency('pybind11', required : true)

pycefast_cpp_args = []
if meson.get_compiler('cpp').get_id() == 'msvc'
    pycefast_cpp_args += ['/bigobj']
endif
