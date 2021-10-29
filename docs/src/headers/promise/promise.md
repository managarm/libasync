---
short-description: Promise type
...

# promise

`promise` is a type that can be used to set a value that can be awaited via a `future`.
The stored data is only destroyed when the promise, and all associated futures go out
of scope. A promise is movable and non-copyable.

## Prototype

```cpp
template <typename T, typename Allocator>
struct promise {
	future<T, Allocator> get_future(); // (1)

	template <typename U>
	void set_value(U &&v); // (2)
	void set_value(); // (3)
};
```

1. Obtains the future associated with this promise. This can be called multiple times to
get multiple futures.
2. Emplaces a value into the promise (overload for `T` other than `void`).
3. Same as (2), except `T` must be `void`.

### Requirements

`T` is any type. `Allocator` is an allocator. `T` is constructible from `U`.

### Arguments

 - `v` - the value to emplace.

### Return values

1. This method returns a future object associated with the promise.
2. This method doesn't return any value.
3. Same as (2).
