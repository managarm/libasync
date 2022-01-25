project('libasync', 'cpp',
	default_options : [
		'cpp_std=c++20'
	])

libasync_inc = include_directories('include/')
libasync_dep = declare_dependency(include_directories : libasync_inc)

if get_option('install_headers')
	install_headers(
		'include/async/algorithm.hpp',
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
		subdir : 'async/')
endif

if get_option('build_tests')
	subdir('tests')
endif

if get_option('enable_docs')
	hotdoc = import('hotdoc')
	docs = hotdoc.generate_doc('libasync',
		project_version : '0',
		sitemap : 'docs/sitemap',
		index : 'docs/src/index.md',
		extra_assets : [ 'docs/assets/' ]
	)

	run_target('build-doc',
		command : [ files('docs/run_hotdoc'), docs.config_path() ]
	)
endif
