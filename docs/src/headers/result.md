# result

```cpp
#include <async/result.hpp>
```

`result` is a generic coroutine promise and sender type. It it used for coroutines
for which you need to await the result of.

## Prototype

```cpp
template <typename T>
struct result;
```

### Requirements

`T` is the type of the value returned by the coroutine.

### Examples

```cpp
async::result<int> coro1(int i) {
	co_return i + 1;
}

async::result<int> coro2(int i) {
	co_return i * co_await coro1(i);
}

int main() {
	std::cout << async::run(coro2(5)) << std::endl;
}
```

Output:
```
30
```
