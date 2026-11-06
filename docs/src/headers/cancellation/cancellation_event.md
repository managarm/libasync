# cancellation_event and cancellation_token

`cancellation_event` is a type that is used to request cancellation of an
asynchronous operation.

`cancellation_token` is a type constructible from a `cancellation_event` that's
used by operations to check for cancellation.

## Prototype

```cpp
struct cancellation_event {
	cancellation_event(const cancellation_event &) = delete;
	cancellation_event(cancellation_event &&) = delete;

	cancellation_event &operator=(const cancellation_event &) = delete;
	cancellation_event &operator=(cancellation_event &&) = delete;

	void cancel(); // (1)
	[[nodiscard]] deferred_cancellation cancel_deferred(); // (2)
	void cancel_deferred(deferred_cancellation &deferred); // (3)
	void reset(); // (4)
};

struct cancellation_token {
	cancellation_token(); // (5)
	cancellation_token(cancellation_event &ev); // (6)

	bool is_cancellation_requested(); // (7)
};
```

1. Requests cancellation.
2. Requests cancellation but does not invoke the callbacks; see
[deferred_cancellation](deferred_cancellation.md).
3. Same as (2), but adds the callbacks to an existing `deferred_cancellation`.
4. Resets the object to prepare it for reuse.
5. Default constructs a cancellation token.
6. Constructs a cancellation token from a cancellation event.
7. Checks whether cancellation is requested.

### Requirements

 - `reset()` must only be called if external synchronization guarantees that
   nobody uses the event anymore. In particular, no callbacks may be attached to
   the event (or be pending in a `deferred_cancellation`) and no cancellation may
   be requested concurrently.

### Arguments

 - `deferred` - the `deferred_cancellation` that receives the callbacks.
 - `ev` - the cancellation event to use.

### Return values

1. This method doesn't return any value.
2. Returns the callbacks that still need to be invoked.
3. Same as (1).
4. Same as (1).
5. N/A
6. N/A
7. Returns `true` if cancellation was requested, `false` otherwise.

## Example

```cpp
async::run([] () -> async::result<void> {
	async::queue<int, frg::stl_allocator> q;

	async::cancellation_event ce;
	ce.cancel(); // Request cancellation immediately

	auto v = q.async_get(ce);
	if (!v)
		std::cout << "Cancelled" << std::endl;
	else
		std::cout << "Completed: " << *v << std::endl;
}());
```

Output:

```
Cancelled
```
