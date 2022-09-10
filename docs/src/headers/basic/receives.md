# `concept Receives`

The `Receives<T>` concept holds all the requirements for a
[receiver](/sender-receiver.md) that can receive a `T` value (or none, when `T`
is `void`).

## Prototype

```cpp
template<typename T, typename Receives>
concept Receives = ...;
```

### Requirements

A `set_value_inline` and `set_value_noinline` members, which can be called with
a `T&&` value, or no parameters, if `T` is `void`.

## Examples

```cpp
struct discard_receiver {
	template<typename T>
	void set_value_inline(T) {
		assert(std::is_constant_evaluated());
	}
	void set_value_inline() {
		assert(std::is_constant_evaluated());
	}

	template<typename T>
	void set_value_noinline(T) {
		assert(std::is_constant_evaluated());
	}
	void set_value_noinline() {
		assert(std::is_constant_evaluated());
	}
};
static_assert(async::Receives<discard_receiver, void>);
static_assert(async::Receives<discard_receiver, int>);
static_assert(async::Receives<discard_receiver, std::string>);
```
