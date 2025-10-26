# run and run_forever

`run` and `run_forever` are top-level functions used for running coroutines. `run`
runs a coroutine until it completes, while `run_forever` runs coroutines indefinitely
via the IO service.

## Prototype

```cpp
template<typename IoService>
void run_forever(IoService ios); // (1) 

template<typename Sender>
Sender::value_type run(Sender s); // (2)

template<typename Sender, typename IoService>
Sender::value_type run(Sender s, IoService ios); // (3)
```

1. Run the IO service indefinitely
2. Start the sender and wait until it completes. The sender **must** complete
inline as there's no way to wait for it to complete.
3. Same as (2) but the sender can complete not-inline.

### Requirements

`IoService` is an [IO service](../../io-service.md), and `Sender` is a sender.

### Arguments

 - `IoService` - the IO service to use to wait for completion.
 - `Sender` - the sender to start.

### Return value

1. This function does not return.
2. This function returns the result value obtained from the sender.
3. Same as (2).

## Examples

```cpp
int i = async::run([] () -> async::result<int> {
			co_return 1;
		}());
std::cout << i << std::endl;
```

Output:
```
1
```
