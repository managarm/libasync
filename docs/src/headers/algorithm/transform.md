# transform

`transform` is an operation that starts the given sender, and upon completion
applies the functor to it's return value.

## Prototype

```cpp
template <typename Sender, typename F>
sender transform(Sender ds, F f);
```

### Requirements

`Sender` is a sender and `F` is a functor that accepts the return value of the
sender as an argument.

### Arguments

 - `ds` - the sender whose result value should be transformed.
 - `f` - the functor to use.

### Return value

This function returns a sender of unspecified type. This sender returns the return
value of the functor.

## Examples

```cpp
auto coro = [] () -> async::result<int> {
	co_return 5;
};

std::cout << async::run(async::transform(coro(), [] (int i) { return i * 2; }) << std::endl;
```

Output:
```
10
```
