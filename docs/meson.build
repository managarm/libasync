mdbook = find_program('mdbook', required: get_option('build_docs'))

summary({'docs': mdbook.found()})
if not mdbook.found()
	subdir_done()
endif
summary({'mdbook': mdbook})

custom_target('build-doc',
	build_always : true,
	build_by_default : true,
	input : [
		'src/headers/algorithm.md',
		'src/headers/algorithm/invocable.md',
		'src/headers/algorithm/ite.md',
		'src/headers/algorithm/let.md',
		'src/headers/algorithm/race_and_cancel.md',
		'src/headers/algorithm/repeat_while.md',
		'src/headers/algorithm/sequence.md',
		'src/headers/algorithm/transform.md',
		'src/headers/algorithm/when_all.md',
		'src/headers/algorithm/lambda.md',
		'src/headers/basic.md',
		'src/headers/basic/any_receiver.md',
		'src/headers/basic/detached.md',
		'src/headers/basic/run.md',
		'src/headers/basic/sender_awaiter.md',
		'src/headers/basic/spawn.md',
		'src/headers/cancellation.md',
		'src/headers/cancellation/cancellation_callback.md',
		'src/headers/cancellation/cancellation_event.md',
		'src/headers/cancellation/suspend_indefinitely.md',
		'src/headers/execution.md',
		'src/headers/mutex.md',
		'src/headers/mutex/mutex.md',
		'src/headers/mutex/shared_mutex.md',
		'src/headers/oneshot-event.md',
		'src/headers/post-ack.md',
		'src/headers/post-ack/post_ack_agent.md',
		'src/headers/post-ack/post_ack_handle.md',
		'src/headers/post-ack/post_ack_mechanism.md',
		'src/headers/promise.md',
		'src/headers/promise/future.md',
		'src/headers/promise/promise.md',
		'src/headers/queue.md',
		'src/headers/recurring-event.md',
		'src/headers/result.md',
		'src/headers/sequenced-event.md',
		'src/contributing.md',
		'src/headers.md',
		'src/io-service.md',
		'src/sender-receiver.md',
		'src/your-own-sender.md',
		'src/index.md',
	],
	output : 'libasync-doc',
	command : [mdbook, 'build', '@CURRENT_SOURCE_DIR@', '-d', meson.current_build_dir() / 'libasync-doc']
)
