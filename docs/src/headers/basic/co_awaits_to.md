# co_awaits_to

`co_awaits_to` is a concept that checks whether an expression of the
given type is `co_await`-able, and awaits to a value of the specified
type.

## Prototype

```cpp
template<typename Awaitable, typename T>
concept co_awaits_to;
```

### Arguments

 - `Awaitable` - the type to check.
 - `T` - the type the awaitable is supposed to await to.

## Examples

```cpp
template <typename T>
concept foo = requires (T t) {
	{ t.foo() } -> async::co_awaits_to<int>;
};
```
