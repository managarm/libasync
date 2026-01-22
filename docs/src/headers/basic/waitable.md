# `concept Waitable`

The `Waitable` concept holds all the requirements for an [IO
service](/io-service.md). Presently, this is only a `wait()` method.

## Prototype

```cpp
template<typename T>
concept Waitable = ...;
```

### Requirements

`T` provides a instance wait method that can be called on a value.

## Examples

```cpp
struct io_service {
/** \pre loop must still be alive */
void wait() {
    auto loop = m_loop.lock();
    assert(loop);
    loop->wait();
}
private:
friend struct event;
io_service(std::weak_ptr<event> e) : m_loop { std::move(e) } {}
std::weak_ptr<event> m_loop;
};
static_assert(async::Waitable<io_service>);
```
