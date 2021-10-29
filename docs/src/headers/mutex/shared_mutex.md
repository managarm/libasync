---
short-description: Asynchronous shared mutex
...

# shared_mutex

`shared_mutex` is a mutex which supports asynchronous acquisition. It can be
acquired in either shared mode, or exclusive mode. Acquiring in exclusive mode
blocks until all shared owners, or the current exclusive owner release the mutex,
and acquiring in shared mode only blocks if it's currently owned exclusively.
When the mutex is contended, the operation asynchronously blocks until the current
holder releases it. These are also known as read-write mutexes, where shared mode
is a read, and exclusive mode is a write.

## Prototype

```cpp
struct shared_mutex {
	sender async_lock(); // (1)

	sender async_lock_shared(); // (2)

	void unlock(); // (3)

	void unlock_shared(); // (4)
};
```

1. Asynchronously acquire the mutex in exclusive mode.
1. Asynchronously acquire the mutex in shared mode.
3. Release the mutex (mutex must be in exclusive mode).
3. Release the mutex (mutex must be in shared mode).

### Return values
1. This method returns a sender of unspecified type. The sender does not return
any value, and completes once the mutex is acquired.
2. Same as (1).
3. This method doesn't return any value.
4. Same as (3).

## Examples

```cpp
async::shared_mutex mtx;
async::queue<int, frg::stl_allocator> q;

auto coro_shared = [] (int i, auto &mtx, auto &q) -> async::detached {
	std::cout << i << ": taking shared" << std::endl;
	co_await mtx.async_lock_shared();
	std::cout << i << ": acquired shared" << std::endl;
	co_await q.async_get();
	std::cout << i << ": releasing shared" << std::endl;
	mtx.unlock_shared();
};

auto coro_exclusive = [] (int i, auto &mtx, auto &q) -> async::detached {
	std::cout << i << ": taking exclusive" << std::endl;
	co_await mtx.async_lock();
	std::cout << i << ": acquired exclusive" << std::endl;
	co_await q.async_get();
	std::cout << i << ": releasing exclusive" << std::endl;
	mtx.unlock();
};

coro_shared(1, mtx, q);
coro_shared(2, mtx, q);
coro_exclusive(3, mtx, q);
coro_exclusive(4, mtx, q);

q.put(1);
q.put(2);
q.put(3);
q.put(4);
```

Output:
```
1: taking shared
1: acquired shared
2: taking shared
2: acquired shared
3: taking exclusive
4: taking exclusive
1: releasing shared
2: releasing shared
3: acquired exclusive
3: releasing exclusive
4: acquired exclusive
4: releasing exclusive
```
