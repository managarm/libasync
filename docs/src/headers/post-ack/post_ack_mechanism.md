---
short-description: post-ack producer class
...

# post_ack_mechanism

`post_ack_mechanism` is the producer class of post-ack. It can asynchronously
post a value, which will only complete once all consumers acknowledge the value.

## Prototype

```cpp
template <typename T>
struct post_ack_mechanism {
	sender post(T object); // (1)
};
```

1. Asynchronously post a value.

### Requirements

`T` is moveable.

### Arguments

 - `object` - the value to post.

### Return values

1. This method returns a sender of unspecified type. The sender doesn't return any value.
