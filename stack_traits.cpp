
#include <cstddef>
#include <assert.h>

namespace ctx
{

struct stack_traits
{
	static bool is_unbounded() noexcept;

	static std::size_t page_size() noexcept;

	static std::size_t default_size() noexcept;

	static std::size_t minimum_size() noexcept;

	static std::size_t maximum_size() noexcept;
};
} // namespace ctx
extern "C"
{
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
}

#include <algorithm>
#include <cmath>
#include <mutex>

namespace
{

void pagesize_(std::size_t* size) noexcept
{
	// conform to POSIX.1-2001
	*size = ::sysconf(_SC_PAGESIZE);
}

void stacksize_limit_(rlimit* limit) noexcept
{
	// conforming to POSIX.1-2001
	::getrlimit(RLIMIT_STACK, limit);
}

std::size_t pagesize() noexcept
{
	static std::size_t size = 0;
	static std::once_flag flag;
	std::call_once(flag, pagesize_, &size);
	return size;
}

rlimit stacksize_limit() noexcept
{
	static rlimit limit;
	static std::once_flag flag;
	std::call_once(flag, stacksize_limit_, &limit);
	return limit;
}

} // namespace

namespace ctx
{

bool stack_traits::is_unbounded() noexcept
{
	return RLIM_INFINITY == stacksize_limit().rlim_max;
}

std::size_t stack_traits::page_size() noexcept
{
	return pagesize();
}

std::size_t stack_traits::default_size() noexcept
{
	return 128 * 1024;
}

std::size_t stack_traits::minimum_size() noexcept
{
	return MINSIGSTKSZ;
}

std::size_t stack_traits::maximum_size() noexcept
{
	assert(!is_unbounded());
	return static_cast<std::size_t>(stacksize_limit().rlim_max);
}

} // namespace ctx
