cpp = meson.get_compiler('cpp')
if cpp.get_id() != 'clang'
    error('fuzzing needs clang: libFuzzer is a clang runtime (configure with CXX=clang++)')
endif

# One sanitizer set for all targets: fuzzer drives the loop, the other two turn a bug into a
# readable report instead of a wrong answer.
fuzz_sanitizers = ['-fsanitize=fuzzer,address,undefined', '-fno-omit-frame-pointer']

foreach target : ['load', 'time', 'float', 'lexer']
    executable(
        'fuzz_' + target,
        'fuzz_' + target + '.cpp',
        dependencies : [cefast_dep],
        include_directories : include_directories('.'),
        cpp_args : fuzz_sanitizers,
        link_args : fuzz_sanitizers
    )
endforeach
