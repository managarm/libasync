---
short-description: Brief overview of a custom sender and operation
...

# Writing your own sender and operation

While libasync provides a veriaty of existing types, one may wish to write their
own senders and operations. The most common reason for writing a custom sender
and operation is wrapping around an existing callback or event based API while
avoiding extra costs induced by allocating coroutine frames. The following section
goes into detail on how to implement them by implementing a simple wrapper for libuv's `uv_write`.

## End effect

The sender and operation we'll implement here will allow us to do the following:
```cpp
auto result = co_await write(my_handle, my_bufs, n_my_bufs);
if (result < 0)
	/* report error */
```

## The implementation

### The sender

As explained in [](sender-receiver.md), the sender is an object that stores the
neceessary state to start an operation. Let's start off by looking at the
prototype for `uv_write`:
```cpp
int uv_write(uv_write_t *req, uv_stream_t *handle, const uv_buf_t bufs[], unsigned int nbufs, uv_write_cb cb);
```

The function takes some object that stores the state (not to be confused with our
operation object), a handle to the stream, buffers to write and a callback. The
callback is also given a status code, which we will propagate back to the user.
The state and callback will be handled by our operation, which leaves only the
handle, buffers and status code.

Let's start by writing a simple class:
```cpp
struct [[nodiscard]] write_sender {
	using value_type = int; // Status code

	uv_stream_t *handle;
	const uv_buf_t *bufs;
	size_t nbufs;
};
```

As can be seen, the sender class is quite simple. The `nodiscard` attribute only
helps catch errors caused by accidentally ignoring the sender without awaiting
it and can be omitted.

Next we add a simple function used to construct the sender:
```cpp
write_sender write(uv_stream_t *handle, const uv_buf_t *bufs, size_t nbufs) {
	return write_sender{handle, bufs, nbufs};
}
```

In addition to that, we need the `connect` overload:
```cpp
template <typename Receiver>
write_operation<Receiver> connect(write_sender s, Receiver r) {
	return {s, std::move(r)};
}
```

`connect` simply constructs an operation from the sender and receiver.

We also add an implementation of `operator co_await` for our class so that we
can `co_await` it inside of a coroutine:
```cpp
async::sender_awaiter<write_sender, write_sender::value_type>
operator co_await(write_sender s) {
	return {s};
}
```

[`async::sender_awaiter`](headers/basic/sender_awaiter.md) is a special type
that can suspend and resume the coroutine, and internally connects a receiver
to our sender.

### The operation

With the sender done, what remains to be written is the operation. As noted earlier,
the operation is constructed using the sender and receiver, and it stores the
operation state. As such, we want each call to `write` to have an unique operation
object. Let's start by writing a skeleton for the class:
```cpp
template <typename Receiver>
struct write_operation {
	write_operation(write_sender s, Receiver r)
	: req_{}, handle_{s.handle}, bufs_{s.bufs}, nbufs_{s.nbufs}, r_{std::move(r)} { }

	write_operation(const write_operation &) = delete;
	write_operation &operator=(const write_operation &) = delete;
	write_operation(write_operation &&) = delete;
	write_operation &operator=(write_operation &&) = delete;

private:
	uv_write_t req_;
	uv_stream_t *handle_;
	const uv_buf_t *bufs_;
	size_t nbufs_;

	Receiver r_;
};
```

The operation stores all the necessary state, and is templated on the receiver
type in order to support any receiver type.

The operation is also made immovable and non-copyable so that pointers to it can
safely be taken without worrying that they may become invalid at some point.

Next, we add a `start_inline` method:
```cpp
bool start_inline() {
	auto result = uv_write(&req_, handle_, bufs_, nbufs_, [] (uv_write_t *req, int status) {
		/* TODO */
	});

	if (result < 0) {
		async::execution::set_value_inline(r_, result);
		return true; // Completed inline
	}

	return false; // Did not complete inline
}
```

We use `start_inline` here in order to notify the user of any immediate errors
synchronously. We use functions defined inside of `async::execution` to set the
value, because they properly detect which method should be called on the receiver.

Now, let's implement the actual asynchronous completion:
```cpp
...
	handle_->data = this;
	auto result = uv_write(&req_, handle_, bufs_, nbufs_, [] (uv_write_t *req, int status) {
		auto op = static_cast<write_operation *>(req->handle->data);
		op->complete(status);
	});
...
```

We use the handle's user data field to store a pointer to this instance of the operation
in order to be able to access it later. This is necessary as otherwise we'd have no way
of knowing which operation caused our callback to be entered. Do note that this way
of implementing it means that only one write operation may be in progress at once.
One way to solve this would be to have a wrapper object that manages the handle and
has a map of `req` to `write_operation`, but that's beyond the scope of this example.

**Note:** Due to how libuv operates (it hides the actual event loop and instead
dispatches callbacks directly), the [](io-service.md) will not have any logic apart
from invoking `uv_run` to do one iteration of the event loop.

Finally, we add our `complete` method:
```cpp
private:
	void complete(int status) {
		async::execution::set_value_noinline(r_, status);
	}
```

On `complete`, we use `async::execution::set_value_noinline` to set the result
value and notify the receiver that the operation is complete (so that it can
for example resume the suspended coroutine, like the `async::sender_awaiter` receiver).

### Full code

All of this put together gives us the following code:
```cpp
// ----------------------------------------------
// Sender
// ----------------------------------------------

struct [[nodiscard]] write_sender {
	using value_type = int; // Status code

	uv_stream_t *handle;
	const uv_buf_t *bufs;
	size_t nbufs;
};

write_sender write(uv_stream_t *handle, const uv_buf_t *bufs, size_t nbufs) {
	return write_sender{handle, bufs, nbufs};
}

template <typename Receiver>
write_operation<Receiver> conneect(write_sender s, Receiver r) {
	return {s, std::move(r)};
}

async::sender_awaiter<write_sender, write_sender::value_type>
operator co_await(write_sender s) {
	return {s};
}

// ----------------------------------------------
// Operation
// ----------------------------------------------

template <typename Receiver>
struct write_operation {
	write_operation(write_sender s, Receiver r)
	: req_{}, handle_{s.handle}, bufs_{s.bufs}, nbufs_{s.nbufs}, r_{std::move(r)} { }

	write_operation(const write_operation &) = delete;
	write_operation &operator=(const write_operation &) = delete;
	write_operation(write_operation &&) = delete;
	write_operation &operator=(write_operation &&) = delete;

	bool start_inline() {
		handle_->data = this;
		auto result = uv_write(&req_, handle_, bufs_, nbufs_, [] (uv_write_t *req, int status) {
			auto op = static_cast<write_operation *>(req->handle->data);
			op->complete(status);
		});

		if (result < 0) {
			async::execution::set_value_inline(r_, result);
			return true; // Completed inline
		}

		return false; // Did not complete inline
	}

private:
	void complete(int status) {
		async::execution::set_value_noinline(r_, status);
	}

	uv_write_t req_;
	uv_stream_t *handle_;
	const uv_buf_t *bufs_;
	size_t nbufs_;

	Receiver r_;
};
```
