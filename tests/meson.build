catch2_dep = dependency('catch2-with-main', version : '>3.0.0', required : true,
    default_options : ['tests=false'])

test_time = executable('test_time', 'cpp/test_time.cpp',
    dependencies : [cefast_dep, catch2_dep])

test('time', test_time, args : ['-o', 'time_out.xml', '-r', 'xml'])
