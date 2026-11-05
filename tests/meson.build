catch2_dep = dependency('catch2-with-main', version : '>3.0.0', required : true,
    default_options : ['tests=false'])

test_resources = meson.project_source_root() / 'tests' / 'resources'
test_defines = '-DCEFAST_TEST_RESOURCES="' + test_resources + '"'

test_time = executable('test_time', 'cpp/test_time.cpp',
    dependencies : [cefast_dep, catch2_dep])
test('time', test_time, args : ['-o', 'time_out.xml', '-r', 'xml'])

test_input = executable('test_input', 'cpp/test_input.cpp',
    dependencies : [cefast_dep, catch2_dep],
    cpp_args : [test_defines])
test('input', test_input, args : ['-o', 'input_out.xml', '-r', 'xml'])
