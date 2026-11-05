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

test_lexer = executable('test_lexer', 'cpp/test_lexer.cpp',
    dependencies : [cefast_dep, catch2_dep])
test('lexer', test_lexer, args : ['-o', 'lexer_out.xml', '-r', 'xml'])

test_header = executable('test_header', 'cpp/test_header.cpp',
    dependencies : [cefast_dep, catch2_dep],
    cpp_args : [test_defines])
test('header', test_header, args : ['-o', 'header_out.xml', '-r', 'xml'])

test_values = executable('test_values', 'cpp/test_values.cpp',
    dependencies : [cefast_dep, catch2_dep],
    cpp_args : [test_defines])
test('values', test_values, args : ['-o', 'values_out.xml', '-r', 'xml'])

test_records = executable('test_records', 'cpp/test_records.cpp',
    dependencies : [cefast_dep, catch2_dep],
    cpp_args : [test_defines])
test('records', test_records, args : ['-o', 'records_out.xml', '-r', 'xml'])

test_corpus = executable('test_corpus', 'cpp/test_corpus.cpp',
    dependencies : [cefast_dep, catch2_dep],
    cpp_args : [test_defines])
test('corpus', test_corpus, args : ['-o', 'corpus_out.xml', '-r', 'xml'])

if get_option('python')
  python3 = import('python').find_installation('python3')
  test('python', python3,
      args : [files('python/test_cefast.py')],
      env : ['PYTHONPATH=' + meson.project_build_root(),
             'CEFAST_TEST_RESOURCES=' + test_resources],
      depends : _cefast)

  test('ceflib', python3,
      args : [files('python/test_vs_ceflib.py')],
      env : ['PYTHONPATH=' + meson.project_build_root(),
             'CEFAST_TEST_RESOURCES=' + test_resources],
      depends : _cefast,
      timeout : 600)
endif
