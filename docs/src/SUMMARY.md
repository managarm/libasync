# Summary

[libasync](index.md)
- [Contributing](contributing.md)
- [Senders, receivers, and operations](sender-receiver.md)
  - [Writing your own sender and operation](your-own-sender.md)
- [IO services](io-service.md)
- [API reference](headers.md)
  - [async/algorithm.hpp](headers/algorithm.md)
    - [invocable](headers/algorithm/invocable.md)
    - [transform](headers/algorithm/transform.md)
    - [ite](headers/algorithm/ite.md)
    - [repeat\_while](headers/algorithm/repeat_while.md)
    - [race\_and\_cancel](headers/algorithm/race_and_cancel.md)
    - [let](headers/algorithm/let.md)
    - [sequence](headers/algorithm/sequence.md)
    - [when\_all](headers/algorithm/when_all.md)
    - [lambda](headers/algorithm/lambda.md)
  - [async/basic.hpp](headers/basic.md)
    - [co\_awaits\_to](headers/basic/co_awaits_to.md)
    - [sender\_awaiter](headers/basic/sender_awaiter.md)
    - [any\_receiver](headers/basic/any_receiver.md)
    - [run and run_forever](headers/basic/run.md)
    - [detached](headers/basic/detached.md)
    - [spawn](headers/basic/spawn.md)
  - [async/result.hpp](headers/result.md)
  - [async/oneshot.hpp](headers/oneshot-event.md)
  - [async/wait-group.hpp](headers/wait-group.md)
    - [wait\_group](headers/wait-group/wait_group.md)
    - [wait\_in\_group](headers/wait-group/wait_in_group.md)
  - [async/recurring.hpp](headers/recurring-event.md)
  - [async/sequenced.hpp](headers/sequenced-event.md)
  - [async/cancellation.hpp](headers/cancellation.md)
    - [cancellation\_event](headers/cancellation/cancellation_event.md)
    - [cancellation\_callback](headers/cancellation/cancellation_callback.md)
    - [suspend\_indefinitely](headers/cancellation/suspend_indefinitely.md)
  - [async/execution.hpp](headers/execution.md)
  - [async/queue.hpp](headers/queue.md)
  - [async/mutex.hpp](headers/mutex.md)
    - [mutex](headers/mutex/mutex.md)
    - [shared\_mutex](headers/mutex/shared_mutex.md)
  - [async/promise.hpp](headers/promise.md)
    - [promise](headers/promise/promise.md)
    - [future](headers/promise/future.md)
  - [async/post.hpp](headers/post-ack.md)
    - [post\_ack\_mechanism](headers/post-ack/post_ack_mechanism.md)
    - [post\_ack\_agent](headers/post-ack/post_ack_agent.md)
    - [post\_ack\_handle](headers/post-ack/post_ack_handle.md)
