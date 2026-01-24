# any_receiver

`any_receiver` is a wrapper type that wraps around any receiver type and handles
calling `set_value` on it.

## Prototype

```cpp
template <typename T>
struct any_receiver {
	template <typename R>
	any_receiver(R receiver); // (1)

	void set_value(T); // (2)
}
```

1. Constructs the object with the given receiver.
2. Forwards the value to the given receiver.

### Requirements

`T` is any type, `R` is a receiver that accepts values of type `T`. `R` is trivially
copyable, and is smaller or of the same size and alignment as a `void *`.

### Arguments

 - `receiver` - the receiver to wrap.

### Return values

1. N/A
2. These methods don't return any value.
