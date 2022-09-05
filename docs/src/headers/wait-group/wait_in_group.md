# wait\_in\_group

`wait_in_group(wg, S)` takes a sender `S` and adds it to the work group `wg`
(calls `wg.add(1)`) immediately before it's started and marks it as done
(calls `wg.done()`) immediately after.

## Prototype

```cpp
template<typename S>
sender wait_in_group(wait_group &wg, S sender);
```

## Requirements
`S` is a sender.

## Arguments
- `wg` - wait group to wait in
- `sender` - sender to wrap in the wait group

## Return value
The value produced by the `sender`.

## Examples

```cpp
bool should_run() {
	/* ... */
}
async::result<void> handle_conn(tcp_socket conn) {
	/* ... */
}

/* ... */

tcp_socket server;
server.bind(":80");
server.listen(32);
async::wait_group handlers { 0 };
while (should_run()) {
	auto conn = socket.accept();
	async::detach(async::wait_in_group(handlers, handle_conn(std::move(conn))));
}

/* wait for all connections to terminate */
handlers.wait();
```
