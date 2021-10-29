---
short-description: post-ack value handle class
...

# post_ack_handle

`post_ack_handle` is the value handle class of post-ack. It has methods to obtain
a pointer to the value and to acknowledge the value.

## Prototype

```cpp
template <typename T>
struct post_ack_handle {
	friend void swap(post_ack_handle &a, post_ack_handle &b); // (1)

	explicit post_ack_handle() = default; // (2)

	post_ack_handle(const post_ack_handle &) = delete;
	post_ack_handle(post_ack_handle &&); // (3)

	post_ack_handle &operator=(const post_ack_handle &) = delete;
	post_ack_handle &operator=(post_ack_handle &&); // (3)

	void ack(); // (4)

	explicit operator bool (); // (5)

	T *operator-> (); // (6)

	T &operator* (); // (6)
};
```

1. Swaps two handles.
2. Constructs an empty handle.
3. Moves a handle.
4. Acknowledges the value.
5. Checks if the handle is valid.
6. Gets the stored object.

### Requirements

`T` is moveable.

### Return values

1. This function doesn't return any value.
2. N/A
3. N/A for constructor, reference to the handle for assignment operator.
4. This method doesn't return any value.
5. This method returns `true` if the handle isn't empty, `false` otherwise.
6. These methods return a pointer/reference to the handle.
