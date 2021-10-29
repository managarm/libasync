---
short-description: Classes used to request cancellation of an operation
...

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
	void reset(); // (2)
};

struct cancellation_token {
	cancellation_token(); // (3)
	cancellation_token(cancellation_event &ev); // (4) 

	bool is_cancellation_requested(); // (5)
};
```

1. Requests cancellation.
2. Resets the object to prepare it for reuse.
3. Default constructs a cancellation token.
4. Constructs a cancellation token from a cancellation event.
5. Checks whether cancellation is requested.

### Arguments

 - `ev` - the cancellation event to use.

### Return values

1. This method doesn't return any value.
2. Same as (1).
3. N/A
4. N/A
5. Returns `true` if cancellation was requested, `false` otherwise.

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
