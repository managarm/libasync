# `concept Sender`

The `Sender` concept holds all the requirements for a
[sender](/sender-receiver.md).

## Prototype

```cpp
template<typename T>
concept Sender = ...;
```

### Requirements

`T` has a `value_type`, is move constructible, and can be
[connected](/headers/execution.md).

## Examples

```cpp
struct [[nodiscard]] write_sender {
    using value_type = int; // Status code

    uv_stream_t *handle;
    const uv_buf_t *bufs;
    size_t nbufs;
};

/* operation omitted for brevity */
template <typename Receiver>
/*operation*/<Receiver> connect(write_sender s, Receiver r) {
    return {s, std::move(r)};
}
static_assert(async::Sender<write_sender>);
```
