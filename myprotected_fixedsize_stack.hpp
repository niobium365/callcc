#pragma once

extern "C" {
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
}

#include <cmath>
#include <cstddef>
#include <new>

namespace ctx {

struct stack_traits
{
    static bool is_unbounded() noexcept;

    static std::size_t page_size() noexcept;

    static std::size_t default_size() noexcept;

    static std::size_t minimum_size() noexcept;

    static std::size_t maximum_size() noexcept;
};

template< typename traitsT >
class basic_protected_fixedsize_stack {
private:
    std::size_t     size_;

public:
    typedef traitsT traits_type;

    basic_protected_fixedsize_stack( std::size_t size = traits_type::default_size() ) noexcept :
        size_( size) {
    }

    stack_context allocate() {
        // calculate how many pages are required
        const std::size_t pages(        
            static_cast< std::size_t >(
                std::ceil(
                    static_cast< float >( size_) / traits_type::page_size() ) ) );
        // add one page at bottom that will be used as guard-page
        const std::size_t size__ = ( pages + 1) * traits_type::page_size();

        // conform to POSIX.4 (POSIX.1b-1993, _POSIX_C_SOURCE=199309L)
#if defined(MAP_ANON)
        void * vp = ::mmap( 0, size__, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
#else
        void * vp = ::mmap( 0, size__, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
        if ( MAP_FAILED == vp) throw std::bad_alloc();

        // conforming to POSIX.1-2001
#if defined(BOOST_DISABLE_ASSERTS)
        ::mprotect( vp, traits_type::page_size(), PROT_NONE);
#else
        const int result( ::mprotect( vp, traits_type::page_size(), PROT_NONE) );
        assert( 0 == result);
#endif

        stack_context sctx;
        sctx.size = size__;
        sctx.sp = static_cast< char * >( vp) + sctx.size;
        return sctx;
    }

    void deallocate( stack_context & sctx) noexcept {
        assert( sctx.sp);

        void * vp = static_cast< char * >( sctx.sp) - sctx.size;
        // conform to POSIX.4 (POSIX.1b-1993, _POSIX_C_SOURCE=199309L)
        ::munmap( vp, sctx.size);
    }
};

typedef basic_protected_fixedsize_stack< stack_traits > protected_fixedsize_stack;

}

