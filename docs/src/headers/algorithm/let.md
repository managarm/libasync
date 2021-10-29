---
short-description: The let algorithm
...

# let

`let` is an operation that obtains a value using the given functor, stores it in
a variable, and obtains a sender to start using the second functor, passing a
reference to the variable as an argument.

## Prototype

```cpp
template <typename Pred, typename Func>
sender let(Pred pred, Func func);
```

### Requirements

`Pred` is a functor that returns a value. `Func` is a functor that returns a sender
and accepts a reference to the value returned by `Pred` as an argument.

### Arguments

 - `pred` - the functor to execute to obtain the value.
 - `func` - the function to call to obtain the sender.

### Return value

This function returns a sender of unspecified type. The sender returns the value returned
by the obtained sender.

## Examples

```cpp
std::cout << async::run(async::let([] { return 3; },
			[] (int &i) -> async::result<int> {
				return i * 2;
			})) << std::endl;
```

Output:
```
6
```
