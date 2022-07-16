# promise

```cpp
#include <async/promise.hpp>
```

This header provides promise and future types.

## Examples

```cpp
async::future<int, frg::stl_allocator> future;
{
	async::promise<int, frg::stl_allocator> promise;
	future = promise.get_future();

	promise.set_value(3);
}

std::cout << *async::run(future.get()) << std::endl;
```

Output:
```
3
```
