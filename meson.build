project(
  'cefast',
  'cpp',
  meson_version : '>= 1.2.0',
  version : '0.1.0',
  default_options : ['cpp_std=c++20', 'buildtype=release']
)

cpp_utils_dep = dependency('cpp_utils', fallback : ['cpp_utils', 'cpp_utils_dep'],
    default_options : ['with_tests=false', 'python=false'])
fast_float_dep = dependency('fast_float', fallback : ['fast_float', 'fast_float_dep'])
zlib_dep = dependency('zlib', fallback : ['zlib', 'zlib_dep'])

cefast_inc = include_directories('include')

headers = files(
    [
        'include/cefast/errors.hpp',
        'include/cefast/gzip.hpp',
        'include/cefast/input.hpp',
        'include/cefast/time.hpp'
    ]
)

cefast_dep = declare_dependency(
    include_directories : cefast_inc,
    dependencies : [cpp_utils_dep, fast_float_dep, zlib_dep],
    extra_files : headers
)
meson.override_dependency('cefast', cefast_dep)

install_subdir('include/cefast', install_dir : get_option('includedir'))

if get_option('with_tests')
    subdir('tests')
endif
