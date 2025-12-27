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
deps += dependency('benchmark_main')

exe = executable('bench',
	'oneshot.cpp',
	cpp_args : cpp_args,
	dependencies : deps)

benchmark('bench', exe)
