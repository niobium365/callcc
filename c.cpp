#include <string>
#include <iostream>
#include <coroutine>
#include "mycontinuation_ucontext.hpp"
#include <uv.h>

#define F__(x) std::forward<decltype(x)>(x)
#define M__(x) std::move(x)

using namespace std;

struct CoVoid
{
	struct promise_type
	{
		auto get_return_object()
		{
			return CoVoid{};
		}
		std::suspend_never initial_suspend() noexcept
		{
			return {};
		}
		std::suspend_never final_suspend() noexcept
		{
			return {};
		}
		void unhandled_exception()
		{}
		void return_void()
		{}
	};
};

template <class F>
struct CoSuspendAwaiter
{
	F _f;

	bool await_ready() const
	{
		return false;
	}

	template <class Coro>
	void await_suspend(Coro coro)
	{
		_f(coro);
	}

	void await_resume()
	{}
};
template <typename F>
auto co_async(F&& f)
{
	static_assert(std::is_rvalue_reference_v<F&&>);
	return CoSuspendAwaiter<F>{std::move(f)};
}

void set_timeout(function<void()> f, int ms)
{
	struct d_t
	{
		uv_loop_t* loop = uv_default_loop();
		uv_timer_t timer1;
		function<void()> cb;
		shared_ptr<d_t> holder;
	};

	auto sp = make_shared<d_t>();
	sp->holder = sp;
	sp->cb = M__(f);
	uv_timer_t& timer1 = sp->timer1;

	uv_timer_init(sp->loop, &timer1);
	timer1.data = (void*)sp.get();

	uv_timer_start(
		&timer1,
		[](uv_timer_t* handle)
		{
			d_t* data = (d_t*)(handle->data);

			uv_timer_stop(handle);
			// uv_unref(data->loop);

			auto f = M__(data->cb);
			data->holder = {};
			f();
		},
		ms, 0);
}

auto co_main(ctx::continuation&& c)
{
	perror("==jd==3\n");

	auto ctx = ctx::callcc(
		std::bind([](ctx::continuation& c, ctx::continuation&& c2)
		{
			[](auto c2) -> CoVoid
			{
				co_await co_async([=](auto coro) { //
					set_timeout(coro, 1000);
				});
				c2.resume();
			}(M__(c2));

			return M__(c);
		}, std::ref(c), std::placeholders::_1));

	cout << "==jd==after 1s" << ctx << endl;

	return ctx;
}

int main()
{
	perror("==jd==111111\n");
	auto ctx = ctx::callcc(co_main);

	perror("==jd==2\n");
	cout << "==jd==" << ctx << endl;

	{

		printf("run loop.\n");
		uv_run(uv_default_loop(), UV_RUN_DEFAULT);

		uv_loop_close(uv_default_loop());
	}
	return 0;
}