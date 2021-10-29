---
short-description: post-ack consumer class
...

# post_ack_agent

`post_ack_agent` is the consumer class of post-ack. It can asynchronously poll
for a value produced by the observed [](headers/post-ack/post_ack_mechanism.md).

## Prototype

```cpp
template <typename T>
struct post_ack_agent {
	void attach(post_ack_mechanism<T> *mech); // (1)

	void detach(); // (2)

	sender poll(cancellation_token ct = {}); // (3)
};
```

1. Attach the agent (consumer) to a mechanism (producer).
2. Detach the agent from the mechanism.
1. Asynchronously poll for a value.

### Requirements

`T` is moveable.

### Arguments

 - `mech` - the mechanism to attach to.
 - `ct` - the cancellation token to use.

### Return values

1. This method doesn't return any value.
2. This method doesn't return any value.
3. This method returns a sender of unspecified type. The sender returns a value
of type `post_ack_handle<T>`.
