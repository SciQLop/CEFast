# The package's python files are copied into the build tree so `PYTHONPATH=<builddir>` gives the
# same `cefast/` layout as the installed package (fs.copyfile is a no-op for identical names).
configure_file(input : '__init__.py', output : '__init__.py', copy : true)

_cefast = python3.extension_module('_cefast', files('../pycefast/_cefast.cpp'),
    dependencies : [cefast_dep, pybind11_dep, python3.dependency()],
    cpp_args : pycefast_cpp_args,
    subdir : 'cefast',
    install : true)

python3.install_sources(files('__init__.py'), pure: false, subdir: 'cefast')
