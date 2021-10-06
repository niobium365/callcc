#pragma once

extern "C"
{
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
}

#include <cmath>
#include <cstddef>
#include <new>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace ctx
{
namespace detail
{

inline void pagesize_(std::size_t* size) noexcept
{
	// conform to POSIX.1-2001
	*size = ::sysconf(_SC_PAGESIZE);
}

inline void stacksize_limit_(rlimit* limit) noexcept
{
	// conforming to POSIX.1-2001
	::getrlimit(RLIMIT_STACK, limit);
}

inline std::size_t pagesize() noexcept
{
	static std::size_t size = 0;
	static std::once_flag flag;
	std::call_once(flag, pagesize_, &size);
	return size;
}

inline rlimit stacksize_limit() noexcept
{
	static rlimit limit;
	static std::once_flag flag;
	std::call_once(flag, stacksize_limit_, &limit);
	return limit;
}
} // namespace detail

struct stack_traits
{
	static bool is_unbounded() noexcept
	{
		return RLIM_INFINITY == detail::stacksize_limit().rlim_max;
	}

	static std::size_t page_size() noexcept
	{
		return detail::pagesize();
	}

	static std::size_t default_size() noexcept
	{
		return 128 * 1024;
	}

	static std::size_t minimum_size() noexcept
	{
		return MINSIGSTKSZ;
	}
	static std::size_t maximum_size() noexcept
	{
		assert(!is_unbounded());
		return static_cast<std::size_t>(detail::stacksize_limit().rlim_max);
	}
};

template <typename traitsT>
class basic_protected_fixedsize_stack
{
  private:
	std::size_t size_;

  public:
	typedef traitsT traits_type;

	basic_protected_fixedsize_stack(std::size_t size = traits_type::default_size()) noexcept : size_(size)
	{}

	stack_context allocate()
	{
		// calculate how many pages are required
		const std::size_t pages(
			static_cast<std::size_t>(std::ceil(static_cast<float>(size_) / traits_type::page_size())));
		// add one page at bottom that will be used as guard-page
		const std::size_t size__ = (pages + 1) * traits_type::page_size();

		// conform to POSIX.4 (POSIX.1b-1993, _POSIX_C_SOURCE=199309L)
		void* vp = ::mmap(0, size__, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (MAP_FAILED == vp)
			throw std::bad_alloc();

			// conforming to POSIX.1-2001
		const int result(::mprotect(vp, traits_type::page_size(), PROT_NONE));
		assert(0 == result);

		stack_context sctx;
		sctx.size = size__;
		sctx.sp = static_cast<char*>(vp) + sctx.size;
		return sctx;
	}

	void deallocate(stack_context& sctx) noexcept
	{
		assert(sctx.sp);

		void* vp = static_cast<char*>(sctx.sp) - sctx.size;
		// conform to POSIX.4 (POSIX.1b-1993, _POSIX_C_SOURCE=199309L)
		::munmap(vp, sctx.size);
	}
};

typedef basic_protected_fixedsize_stack<stack_traits> protected_fixedsize_stack;

} // namespace ctx
