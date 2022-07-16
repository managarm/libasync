# ite

`ite` is an operation that checks the given condition, and starts the "then" or
"else" sender depending on the result.

## Prototype

```cpp
template <typename C, typename ST, typename SE>
sender ite(C cond, ST then_s, SE else_s);
```

### Requirements

`C` is a functor that returns a truthy or falsy value. `ST` and `SE` are senders.
`ST` and `SE` must have the same return type.

### Arguments

 - `cond` - the condition to check.
 - `then_s` - the sender to start if condition is true.
 - `else_s` - the sender to start if condition is false.

### Return value

This function returns a sender of unspecified type. The sender returns the value of
the sender that was started depending on the condition.

## Examples

```cpp
auto then_coro = [] () -> async::result<int> {
	co_return 1;
};

auto else_coro = [] () -> async::result<int> {
	co_return 2;
};

std::cout << async::run(async::ite([] { return true; }, then_coro(), else_coro())) << std::endl;
std::cout << async::run(async::ite([] { return false; }, then_coro(), else_coro())) << std::endl;
```

Output:
```
1
2
```
