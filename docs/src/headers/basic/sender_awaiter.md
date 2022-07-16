# sender_awaiter and make_awaiter

`sender_awaiter` is a coroutine promise type that allows for awaiting a sender.
It connects an internal receiver to the given sender, and starts the resulting
operation when the result is awaited.

`make_awaiter` is a function that obtains the awaiter associated with a sender.
It does that by getting the result of `operator co_await`.

## Prototype

```cpp
template <typename S, typename T = void>
struct [[nodiscard]] sender_awaiter {
	sender_awaiter(S sender); // (1)

	bool await_ready(); // (2)
	bool await_suspend(std::coroutine_handle<>); // (2)
	T await_resume(); // (2)
}

template<typename S>
auto make_awaiter(S &&s); // (3)
```

1. Constructs the object with the given sender.
2. Coroutine promise methods.
3. Get the awaiter for the given sender.

### Requirements

`S` is a sender and `T` is `S::value_type` or a type convertible to it.

### Arguments

 - `sender` - the sender to await.

### Return values

1. N/A
2. These methods return implementation-specific values.
3. This function returns the result object of `co_await`ing the given object
(without performing the actual asynchronous await operation).
