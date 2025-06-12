project('libasync', 'cpp',
	default_options : [
		'cpp_std=c++20'
	])

libasync_inc = include_directories('include/')
libasync_dep = declare_dependency(include_directories : libasync_inc)

if get_option('install_headers')
	pkgconfig = import('pkgconfig')

	install_headers(
		'include/async/algorithm.hpp',
		'include/async/barrier.hpp',
		'include/async/basic.hpp',
		'include/async/cancellation.hpp',
		'include/async/execution.hpp',
		'include/async/mutex.hpp',
		'include/async/oneshot-event.hpp',
		'include/async/post-ack.hpp',
		'include/async/promise.hpp',
		'include/async/queue.hpp',
		'include/async/recurring-event.hpp',
		'include/async/result.hpp',
		'include/async/sequenced-event.hpp',
		'include/async/wait-group.hpp',
		subdir : 'async/')

	pkgconfig.generate(
		name : 'libasync',
		description : 'An async primitives library for C++20',
		version : meson.project_version(),
	)
endif

if get_option('build_tests')
	subdir('tests')
endif

subdir('docs')
