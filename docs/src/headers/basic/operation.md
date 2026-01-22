# `concept Operation`

The `Operation` concept holds all the requirements for an
[operation](/sender-receiver.md).

## Prototype

```cpp
template<typename T>
concept Operation = ...;
```

### Requirements

`T` can be started via the [start\_inline](/headers/execution.md) CPO.

## Examples

```cpp
template <typename Receiver>
struct write_operation {
    write_operation(write_sender s, Receiver r)
    : req_{}, handle_{s.handle}, bufs_{s.bufs}, nbufs_{s.nbufs}, r_{std::move(r)} { }

    write_operation(const write_operation &) = delete;
    write_operation &operator=(const write_operation &) = delete;
    write_operation(write_operation &&) = delete;
    write_operation &operator=(write_operation &&) = delete;

    bool start_inline() { /* omitted for brevity */ }

private:
    uv_write_t req_;
    uv_stream_t *handle_;
    const uv_buf_t *bufs_;
    size_t nbufs_;

    Receiver r_;
};
static_assert(async::Operation<write_operation<noop_receiver>>);
```
