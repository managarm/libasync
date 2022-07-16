# post-ack

```cpp
#include <async/post-ack.hpp>
```

This header provides classes for a producer-consumer data structure that requires
every consumer to acknowledge the value before the producer can continue.

## Examples

```cpp
async::post_ack_mechanism<int> mech;

auto producer = [] (async::post_ack_mechanism<int> &mech) -> async::detached {
	std::cout << "Posting 1" << std::endl;
	co_await mech.post(1);
	std::cout << "Posting 2" << std::endl;
	co_await mech.post(2);
};

auto consumer = [] (async::post_ack_mechanism<int> &mech) -> async::detached {
	async::post_ack_agent<int> agent;
	agent.attach(&mech);

	std::cout << "Awaiting first value" << std::endl;
	auto handle = co_await agent.poll();
	std::cout << *handle << ", acking first value" << std::endl;
	handle.ack();

	std::cout << "Awaiting second value" << std::endl;
	handle = co_await agent.poll();
	std::cout << *handle << ", acking second value" << std::endl;
	handle.ack();

	agent.detach();
};

consumer(mech);
consumer(mech);
consumer(mech);

producer(mech);
```

Output:
```
Awaiting first value
Awaiting first value
Awaiting first value
Posting 1
1, acking first value
Awaiting second value
1, acking first value
Awaiting second value
1, acking first value
Posting 2
2, acking second value
2, acking second value
Awaiting second value
2, acking second value
```
