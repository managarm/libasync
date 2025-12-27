cpp_args = []
deps = [ libasync_dep ]

if meson.get_compiler('cpp').has_argument('-fcoroutines')
	cpp_args += '-fcoroutines'
elif meson.get_compiler('cpp').has_argument('-fcoroutines-ts')
	cpp_args += [ '-fcoroutines-ts', '-DLIBASYNC_FORCE_USE_EXPERIMENTAL', '-fsized-deallocation' ]
	deps += subproject('cxxshim').get_variable('clang_coroutine_dep')
else
	error('Unsupported compiler')
endif

deps += subproject('frigg').get_variable('frigg_dep')
deps += dependency('gtest')
deps += dependency('gtest_main')

sources = files(
	'basic.cpp',
	'queue.cpp',
	'mutex.cpp',
	'race.cpp',
	'algorithm.cpp',
	'promise.cpp',
	'sequenced.cpp',
	'post-ack.cpp'
)

exe = executable('gtests',
	sources,
	cpp_args : cpp_args,
	dependencies : deps)

test('gtest test', exe)


