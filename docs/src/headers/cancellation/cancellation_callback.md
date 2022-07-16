# cancellation_callback and cancellation_observer

`cancellation_callback` registers a callback that will be invoked when
cancellation occurs.

`cancellation_observer` checks for cancellation, potentially invoking the handler
and returns whether cancellation occured.

## Prototype

```cpp
template <typename F>
struct cancellation_callback {
	cancellation_callback(cancellation_token ct, F functor = {}); // (1)

	cancellation_callback(const cancellation_callback &) = delete;
	cancellation_callback(cancellation_callback &&) = delete;

	cancellation_callback &operator=(const cancellation_callback &) = delete;
	cancellation_callback &operator=(cancellation_callback &&) = delete;

	void unbind(); // (2)
};

template <typename F>
struct cancellation_observer {
	cancellation_observer(F functor = {}); // (3)

	cancellation_observer(const cancellation_observer &) = delete;
	cancellation_observer(cancellation_observer &&) = delete;

	cancellation_observer &operator=(const cancellation_observer &) = delete;
	cancellation_observer &operator=(cancellation_observer &&) = delete;

	bool try_set(cancellation_token ct); // (4)

	bool try_reset(); // (5)
};
```

1. Constructs and attaches the cancellation callback to the given token.
2. Detaches the cancellation callback.
3. Constructs a cancellation observer.
4. Attaches the cancellation observer to the given token and checks for
cancellation without invoking the functor.
5. Checks for cancellation, potentially invoking the functor, and detaches the
cancellation observer.

### Requirements

`F` is a functor that takes no arguments.

### Arguments

 - `ct` - the cancellation token to attach to.
 - `functor` - the functor to call on cancellation.

### Return values

1. N/A
2. This method doesn't return any value.
3. N/A
4. Returns `false` if cancellation was already requested and the observer hasn't been
attached, `true` otherwise.
5. Same as (4).
