#pragma once

#include <assert.h>
#include <ucontext.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <ostream>
#include <system_error>
#include <tuple>
#include <utility>

#define BOOST_ASSERT_MSG(expr, msg) assert((expr) && (msg))

template <typename X, typename Y>
using disable_overload = typename std::enable_if<!std::is_base_of<X, typename std::decay<Y>::type>::value>::type;

struct stack_context
{
	std::size_t size{0};
	void* sp{nullptr};
};
#include "myprotected_fixedsize_stack.hpp"

namespace ctx
{
namespace detail
{

// tampoline function
// entered if the execution context
// is resumed for the first time
template <typename Record>
static void entry_func(void* data) noexcept
{
	Record* record = static_cast<Record*>(data);
	assert(nullptr != record);
	// start execution of toplevel context-function
	record->run();
}

struct activation_record;
struct activation_record_initializer
{
	inline thread_local static activation_record* current_rec;
	inline thread_local static std::size_t counter;
	activation_record_initializer() noexcept;
	~activation_record_initializer();
};

struct activation_record
{
	ucontext_t uctx{};
	stack_context sctx{};
	bool main_ctx{true};
	activation_record* from{nullptr};
	std::function<activation_record*(activation_record*&)> ontop{};
	bool terminated{false};
	bool force_unwind{false};

	static activation_record*& current() noexcept
	{
		// initialized the first time control passes; per thread
		thread_local static activation_record_initializer initializer;
		return activation_record_initializer::current_rec;
	}

	// used for toplevel-context
	// (e.g. main context, thread-entry context)
	activation_record()
	{
		if ((0 != ::getcontext(&uctx)))
		{
			throw std::system_error(std::error_code(errno, std::system_category()), "getcontext() failed");
		}
	}

	activation_record(stack_context sctx_) noexcept : sctx(sctx_), main_ctx(false)
	{}

	virtual ~activation_record()
	{}

	activation_record(activation_record const&) = delete;
	activation_record& operator=(activation_record const&) = delete;

	bool is_main_context() const noexcept
	{
		return main_ctx;
	}

	activation_record* resume()
	{
		from = current();
		// store `this` in static, thread local pointer
		// `this` will become the active (running) context
		current() = this;

		// context switch from parent context to `this`-context
		::swapcontext(&from->uctx, &uctx);
		return std::exchange(current()->from, nullptr);
	}

	template <typename Ctx, typename Fn>
	activation_record* resume_with(Fn&& fn)
	{
		from = current();
		// store `this` in static, thread local pointer
		// `this` will become the active (running) context
		// returned by continuation::current()
		current() = this;
		current()->ontop = [fn = std::forward<Fn>(fn)](activation_record*& ptr)
		{
			Ctx c{ptr};
			c = fn(std::move(c));
			if (!c)
			{
				ptr = nullptr;
			}
			return std::exchange(c.ptr_, nullptr);
		};

		// context switch from parent context to `this`-context
		::swapcontext(&from->uctx, &uctx);
		return std::exchange(current()->from, nullptr);
	}

	virtual void deallocate() noexcept
	{}
};

inline activation_record_initializer::activation_record_initializer() noexcept
{
	if (0 == counter++)
	{
		current_rec = new activation_record();
	}
}

inline activation_record_initializer::~activation_record_initializer()
{
	if (0 == --counter)
	{
		assert(current_rec->is_main_context());
		delete current_rec;
	}
}

struct forced_unwind
{
	activation_record* from{nullptr};
#ifndef BOOST_ASSERT_IS_VOID
	bool caught{false};
#endif

	forced_unwind(activation_record* from_) noexcept : from{from_}
	{}

#ifndef BOOST_ASSERT_IS_VOID
	~forced_unwind()
	{
		assert(caught);
	}
#endif
};

template <typename Ctx, typename StackAlloc, typename Fn>
class capture_record : public activation_record
{
  private:
	typename std::decay<StackAlloc>::type salloc_;
	typename std::decay<Fn>::type fn_;

	static void destroy(capture_record* p) noexcept
	{
		typename std::decay<StackAlloc>::type salloc = std::move(p->salloc_);
		stack_context sctx = p->sctx;
		// deallocate activation record
		p->~capture_record();
		// destroy stack with stack allocator
		salloc.deallocate(sctx);
	}

  public:
	capture_record(stack_context sctx, StackAlloc&& salloc, Fn&& fn) noexcept
		: activation_record{sctx}, salloc_{std::forward<StackAlloc>(salloc)}, fn_(std::forward<Fn>(fn))
	{}

	void deallocate() noexcept override final
	{
		assert(main_ctx || (!main_ctx && terminated));
		destroy(this);
	}

	void run()
	{
		Ctx c{from};
		try
		{
			// invoke context-function
			c = std::invoke(fn_, std::move(c));
		}
		catch (forced_unwind const& ex)
		{
			c = Ctx{ex.from};
#ifndef BOOST_ASSERT_IS_VOID
			const_cast<forced_unwind&>(ex).caught = true;
#endif
		}
		// this context has finished its task
		from = nullptr;
		ontop = nullptr;
		terminated = true;
		force_unwind = false;
		c.resume();
		BOOST_ASSERT_MSG(false, "continuation already terminated");
	}
};

template <typename Ctx, typename StackAlloc, typename Fn>
static activation_record* create_context1(StackAlloc&& salloc, Fn&& fn)
{
	typedef capture_record<Ctx, StackAlloc, Fn> capture_t;

	auto sctx = salloc.allocate();
	// reserve space for control structure
	void* storage =
		reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(sctx.sp) - static_cast<uintptr_t>(sizeof(capture_t))) &
								~static_cast<uintptr_t>(0xff));
	// placment new for control structure on context stack
	capture_t* record = new (storage) capture_t{sctx, std::forward<StackAlloc>(salloc), std::forward<Fn>(fn)};
	// stack bottom
	void* stack_bottom =
		reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(sctx.sp) - static_cast<uintptr_t>(sctx.size));
	// create user-context
	if ((0 != ::getcontext(&record->uctx)))
	{
		record->~capture_t();
		salloc.deallocate(sctx);
		throw std::system_error(std::error_code(errno, std::system_category()), "getcontext() failed");
	}
	record->uctx.uc_stack.ss_sp = stack_bottom;
	// 64byte gap between control structure and stack top
	record->uctx.uc_stack.ss_size =
		reinterpret_cast<uintptr_t>(storage) - reinterpret_cast<uintptr_t>(stack_bottom) - static_cast<uintptr_t>(64);
	record->uctx.uc_link = nullptr;
	::makecontext(&record->uctx, (void (*)()) & entry_func<capture_t>, 1, record);
	return record;
}

} // namespace detail

class continuation
{
  private:
	friend struct detail::activation_record;

	template <typename Ctx, typename StackAlloc, typename Fn>
	friend class detail::capture_record;

	template <typename Ctx, typename StackAlloc, typename Fn>
	friend detail::activation_record* detail::create_context1(StackAlloc&&, Fn&&);

	template <typename StackAlloc, typename Fn>
	friend continuation callcc(std::allocator_arg_t, StackAlloc&&, Fn&&);

	detail::activation_record* ptr_{nullptr};

	continuation(detail::activation_record* ptr) noexcept : ptr_{ptr}
	{}

  public:
	continuation() = default;

	~continuation()
	{
		if ((nullptr != ptr_) && !ptr_->main_ctx)
		{
			if ((!ptr_->terminated))
			{
				ptr_->force_unwind = true;
				ptr_->resume();
				assert(ptr_->terminated);
			}
			ptr_->deallocate();
		}
	}

	continuation(continuation const&) = delete;
	continuation& operator=(continuation const&) = delete;

	continuation(continuation&& other) noexcept
	{
		swap(other);
	}

	continuation& operator=(continuation&& other) noexcept
	{
		if ((this != &other))
		{
			continuation tmp = std::move(other);
			swap(tmp);
		}
		return *this;
	}

	continuation resume() &
	{
		return std::move(*this).resume();
	}

	continuation resume() &&
	{
		detail::activation_record* ptr = std::exchange(ptr_, nullptr)->resume();
		if ((detail::activation_record::current()->force_unwind))
		{
			throw detail::forced_unwind{ptr};
		}
		else if ((nullptr != detail::activation_record::current()->ontop))
		{
			ptr = detail::activation_record::current()->ontop(ptr);
			detail::activation_record::current()->ontop = nullptr;
		}
		return {ptr};
	}

	template <typename Fn>
	continuation resume_with(Fn&& fn) &
	{
		return std::move(*this).resume_with(std::forward<Fn>(fn));
	}

	template <typename Fn>
	continuation resume_with(Fn&& fn) &&
	{
		detail::activation_record* ptr = std::exchange(ptr_, nullptr)->resume_with<continuation>(std::forward<Fn>(fn));
		if ((detail::activation_record::current()->force_unwind))
		{
			throw detail::forced_unwind{ptr};
		}
		else if ((nullptr != detail::activation_record::current()->ontop))
		{
			ptr = detail::activation_record::current()->ontop(ptr);
			detail::activation_record::current()->ontop = nullptr;
		}
		return {ptr};
	}

	explicit operator bool() const noexcept
	{
		return nullptr != ptr_ && !ptr_->terminated;
	}

	bool operator!() const noexcept
	{
		return nullptr == ptr_ || ptr_->terminated;
	}

	bool operator<(continuation const& other) const noexcept
	{
		return ptr_ < other.ptr_;
	}

	template <typename charT, class traitsT>
	friend std::basic_ostream<charT, traitsT>& operator<<(std::basic_ostream<charT, traitsT>& os,
														  continuation const& other)
	{
		if (nullptr != other.ptr_)
		{
			return os << other.ptr_;
		}
		else
		{
			return os << "{not-a-context}";
		}
	}

	void swap(continuation& other) noexcept
	{
		std::swap(ptr_, other.ptr_);
	}
};

template <typename Fn, typename = disable_overload<continuation, Fn>>
continuation callcc(Fn&& fn)
{
	return callcc(std::allocator_arg, protected_fixedsize_stack(4 * 1024 * 1024), std::forward<Fn>(fn));
}

template <typename StackAlloc, typename Fn>
continuation callcc(std::allocator_arg_t, StackAlloc&& salloc, Fn&& fn)
{
	return continuation{detail::create_context1<continuation>(std::forward<StackAlloc>(salloc), std::forward<Fn>(fn))}
		.resume();
}

inline void swap(continuation& l, continuation& r) noexcept
{
	l.swap(r);
}

} // namespace ctx
