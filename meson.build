project('libasync', 'cpp')

cpp_args = []
coro_dep_name = ''
if meson.get_compiler('cpp').has_argument('-fcoroutines')
	cpp_args += ['-fcoroutines']
	coro_dep_name = 'std_coroutine_dep'
elif meson.get_compiler('cpp').has_argument('-fcoroutines-ts')
	cpp_args = ['-fcoroutines-ts', '-DLIBASYNC_FORCE_USE_EXPERIMENTAL']
	coro_dep_name = 'clang_coroutine_dep'
else
	error('unsupported compiler')
endif

cxxshim_coroutine_dep = subproject('cxxshim').get_variable(coro_dep_name)
frigg_dep = subproject('frigg').get_variable('frigg_dep')
gtest_dep = dependency('gtest', main: true, required: false)

gtest_binary = executable('gtests',
	'tests/basic.cpp',
	'tests/queue.cpp',
	'tests/mutex.cpp',
	'tests/race.cpp',
	'tests/algorithm.cpp',
	'tests/promise.cpp',
	'tests/sequenced.cpp',
	include_directories: include_directories('include/'),
	cpp_args: cpp_args,
	dependencies: [
		cxxshim_coroutine_dep,
		frigg_dep,
		gtest_dep
	],
	override_options: [
		'cpp_std=c++2a'
	])

test('gtest test', gtest_binary)

hotdoc = import('hotdoc')
docs = hotdoc.generate_doc('libasync',
	project_version: '0',
	sitemap: 'docs/sitemap',
	index: 'docs/src/index.md',
	extra_assets: [ 'docs/assets/' ],
)

run_target('build-doc',
	command: [ files('docs/run_hotdoc'), docs.config_path(), ],
)
