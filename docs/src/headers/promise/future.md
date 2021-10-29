---
short-description: Future type
...

# future

`future` is a type that can be used to await a value from the promise. The
stored data is only destroyed when the promise, and all associated futures
go out of scope.

## Prototype

```cpp
template <typename T, typename Allocator>
struct future {
	friend void swap(future &a, future &b); // (1)

	future(Allocator allocator = {}); // (2)

	future(const future &); // (3)
	future(future &&); // (4)

	future &operator=(const future &); // (3)
	future &operator=(future &&); // (4)

	sender get(cancellation_token ct); // (5)
	sender get(); // (6)

	bool valid(); // (7)
	operator bool (); // (7)
};
```

1. Swaps two futures.
2. Constructs an empty future.
3. Copies a future. The copy of the future refers to the same state as the
original object in a manner similar to a `shared_ptr`.
4. Moves a future.
5. Asynchronously obtains the value.
6. Same as (5).
7. Checks whether the future has valid state.

### Requirements

`T` is any type. `Allocator` is an allocator.

### Arguments

 - `ct` - the cancellation token to use.

### Return values

1. This function doesn't return any value.
2. N/A
3. N/A for constructor, reference to this object for assignment operator.
4. Same as (3).
5. This method returns a sender of unspecified type. If `T` is not `void`, the
sender returns a value of type `frg::optional<T *>`. It returns the pointer if
the value was obtained successfully, `frg::null_opt` if the operation was cancelled.
If `T` is `void`, the sender returns `true` if the value was obtained successfully,
   `false` if the operation was cancelled.
6. This method returns a sender of unspecified type. If `T` is not `void`, the
sender returns a pointer to the stored value. If `T` is `void`, it doesn't return anything.
7. These methods return `true` if the future has valid state, `false` otherwise.
