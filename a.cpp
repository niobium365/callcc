#include <string>
#include <iostream>
// #define BOOST_USE_UCONTEXT 1
// #define BOOST_USE_SEGMENTED_STACKS 1
#include "mycontinuation_ucontext.hpp"
#include <uv.h>

#define F__(x) std::forward<decltype(x)>(x)
#define M__(x) std::move(x)

using namespace std;
//namespace ctx = boost::context;

// template <class F>
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

int main()
{
	// dd
	perror("==jd==1\n");
	auto ctx = ctx::callcc(
		[](ctx::continuation&& c)
		{
			//char szBuffer[128 * 1024 * 2] = {'A', 'B'};
			perror("==jd==3\n");

			auto ctx = ctx::callcc(
				[c1 = M__(c)](ctx::continuation&& c2) mutable
				{
					//char szBuffer[128 * 1024 * 2];
					cout << "==jd==set_timeout " << c1 << "x" << c2 << endl;
					set_timeout(
						[up = make_shared<ctx::continuation>(M__(c2))]()
						{
							// main
							perror("==jd==6\n");
							up->resume();
							perror("==jd==7\n");
						},
						1000);

					//szBuffer[128 * 1024 * 2 - 1] = 100;
					return M__(c1);
				});

			string test = "==jd==xxx";
			cout << "==jd==after 1s" << ctx << test << endl;
			
			// c = ctx.resume();
			// c.resume_with([](auto&&a){ return M__(a);});
			// c.resume();
			perror("==jd==4\n");
			//szBuffer[128 * 1024 * 2 - 1] = 100;
			return ctx;
		});

	perror("==jd==2\n");
	cout << "==jd==" << ctx << endl;

	{

		printf("run loop.\n");
		uv_run(uv_default_loop(), UV_RUN_DEFAULT);

		uv_loop_close(uv_default_loop());
	}
	return 0;
}