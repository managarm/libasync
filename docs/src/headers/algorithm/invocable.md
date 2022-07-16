# invocable

`invocable` is an operation that executes the given functor and completes inline
with the value returned by the functor.

## Prototype

```cpp
template <typename F>
sender invocable(F f);
```

### Requirements

`F` is a functor that takes no arguments.

### Arguments

 - `f` - the functor to execute.

### Return value

This function returns a sender of unspecified type. This sender returns the value
returned by the functor.

## Examples

```cpp
auto fn = [] { return 1; };
std::cout << async::run(async::invocable(fn)) << std::endl;
```

Output:
```
1
```
