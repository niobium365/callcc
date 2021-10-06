
#include "mycontinuation_ucontext.hpp"


namespace ctx
{
namespace detail
{

// zero-initialization
thread_local activation_record* current_rec;
thread_local static std::size_t counter;

// schwarz counter
activation_record_initializer::activation_record_initializer() noexcept
{
	if (0 == counter++)
	{
		current_rec = new activation_record();
	}
}

activation_record_initializer::~activation_record_initializer()
{
	if (0 == --counter)
	{
		assert(current_rec->is_main_context());
		delete current_rec;
	}
}

} // namespace detail

namespace detail
{

activation_record*& activation_record::current() noexcept
{
	// initialized the first time control passes; per thread
	thread_local static activation_record_initializer initializer;
	return current_rec;
}

} // namespace detail

} // namespace context
