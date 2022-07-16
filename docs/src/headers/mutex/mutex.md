# mutex

`mutex` is a mutex which supports asynchronous acquisition. When the mutex is
contended, the operation asynchronously blocks until the current holder
releases it.

## Prototype

```cpp
struct mutex {
	sender async_lock(); // (1)

	bool try_lock(); // (2)

	void unlock(); // (3)
};
```

1. Asynchronously acquire the mutex.
2. Synchronously try to acquire the mutex.
3. Release the mutex.

### Return values
1. This method returns a sender of unspecified type. The sender does not return
any value, and completes once the mutex is acquired.
2. This method returns `true` if the mutex was successfully acquired, `false` otherwise.
3. This method doesn't return any value.

## Examples

```cpp
async::mutex mtx;
async::queue<int, frg::stl_allocator> q;

auto coro = [] (int i, auto &mtx, auto &q) -> async::detached {
	std::cout << i << ": taking" << std::endl;
	co_await mtx.async_lock();
	std::cout << i << ": " << mtx.try_lock() << std::endl;
	co_await q.async_get();
	std::cout << i << ": releasing" << std::endl;
	mtx.unlock();
};

coro(1, mtx, q);
coro(2, mtx, q);

q.put(1);
q.put(2);
```

Output:
```
1: taking
1: 0
2: taking
1: releasing
2: 0
2: releasing
```
