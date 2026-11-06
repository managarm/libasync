# deferred_cancellation

`deferred_cancellation` holds the callbacks of cancellation events that were
cancelled by `cancellation_event::cancel_deferred()` but not invoked yet.

Invoking callbacks can run arbitrary code (e.g., completed operations may resume
coroutines inline), so callers that hold locks cannot simply call `cancel()`.
Instead, they cancel the events with `cancel_deferred()` while holding the lock and
call `invoke()` after releasing it. The events count as cancelled as soon as
`cancel_deferred()` returns.

## Prototype

```cpp
struct deferred_cancellation {
	deferred_cancellation(); // (1)
	deferred_cancellation(deferred_cancellation &&other); // (2)

	deferred_cancellation(const deferred_cancellation &) = delete;
	deferred_cancellation &operator=(const deferred_cancellation &) = delete;
	deferred_cancellation &operator=(deferred_cancellation &&) = delete;

	void invoke(); // (3)
};
```

1. Constructs an empty object.
2. Takes over the callbacks of `other`.
3. Invokes all callbacks.

### Requirements

 - `invoke()` must run before the object is destructed.
 - `deferred_cancellation` is not thread-safe: cancelling multiple events into
   the same object from different threads requires external synchronization.
 - A cancellation event must not be destructed while any of its callbacks are
   still pending in the object. Events whose callbacks were all invoked may be
   destructed while `invoke()` is still running.
 - As with a concurrent `cancel()`, `cancellation_callback`s must not be destructed
   before `invoke()` ran. Operations that use `cancellation_observer` or
   `cancellation_resolver` are safe.

### Arguments

 - `other` - the object to take the callbacks from.

### Return values

1. N/A
2. N/A
3. This method doesn't return any value.

## Example

```cpp
std::mutex mutex;
async::cancellation_event ev1, ev2;

async::deferred_cancellation deferred;
{
	std::lock_guard lock{mutex};
	ev1.cancel_deferred(deferred);
	ev2.cancel_deferred(deferred);
}
deferred.invoke();
```
