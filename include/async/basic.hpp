#ifndef LIBASYNC_BASIC_HPP
#define LIBASYNC_BASIC_HPP

#include <mutex>
#include <optional>

namespace async {

template<typename S>
struct callback;

template<typename R, typename... Args>
struct callback<R(Args...)> {
private:
	using storage = std::aligned_storage_t<sizeof(void *), alignof(void *)>;

	template<typename F>
	static R invoke(storage object, Args... args) {
		return (*reinterpret_cast<F *>(&object))(std::move(args)...);
	}

public:
	callback()
	: _function(nullptr) { }

	template<typename F, typename = std::enable_if_t<
			sizeof(F) == sizeof(void *) && alignof(F) == alignof(void *)
			&& std::is_trivially_copy_constructible<F>::value
			&& std::is_trivially_destructible<F>::value>>
	callback(F functor)
	: _function(&invoke<F>) {
		new (&_object) F{std::move(functor)};
	}

	explicit operator bool () {
		return static_cast<bool>(_function);
	}

	R operator() (Args... args) {
		return _function(_object, std::move(args)...);
	}

private:
	R (*_function)(storage, Args...);
	std::aligned_storage_t<sizeof(void *), alignof(void *)> _object;
};

struct awaitable_base {
	awaitable_base()
	: _ready{false} { }

	bool ready() {
		std::lock_guard lock{_mutex};
		return _ready;
	}

	void then(callback<void()> cb) {
		assert(cb);
		{
			std::lock_guard lock{_mutex};
			assert(!_cb);

			if(!_ready)
				_cb = std::exchange(cb, callback<void()>{});
		}

		if(cb)
			cb();
	}

protected:
	void set_ready() {
		callback<void()> cb;
		{
			std::lock_guard lock{_mutex};
			assert(!_ready);

			_ready = true;
			cb = std::exchange(_cb, callback<void()>{});
		}

		if(cb)
			cb();
	}

private:
	// TODO: Use light-weight atomics instead of a mutex.
	std::mutex _mutex;
	bool _ready;
	callback<void()> _cb;
};

template<typename T>
struct awaitable : awaitable_base {
	virtual ~awaitable() { }

	T &value() {
		return _val.value();
	}

protected:	
	template<typename... Args>
	void emplace_value(Args &&... args) {
		_val.emplace(std::forward<Args>(args)...);
	}

private:
	std::optional<T> _val;
};

template<>
struct awaitable<void> : awaitable_base {
	virtual ~awaitable() { }
	
protected:
	void emplace_value() { }
};

template<typename T>
struct cancelable_awaitable : awaitable<T> {
	virtual void cancel() = 0;
};

} // namespace async

#endif // LIBASYNC_BASIC_HPP
