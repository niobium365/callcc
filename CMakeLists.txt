cmake_minimum_required(VERSION 3.19.1)

if(UNIX AND NOT APPLE)
  set(LINUX TRUE)
endif()

set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

add_compile_options(-O0 -ggdb -std=gnu++17)
#add_compile_definitions(BOOST_USE_UCONTEXT=1 BOOST_USE_SEGMENTED_STACKS=1)
add_compile_definitions(BOOST_USE_UCONTEXT=1)
add_compile_options(
	-fcoroutines
)
add_compile_options(
  -v
  -Wall
  -fno-function-sections
  -fno-data-sections
  -Wextra
  -Wno-unused-parameter
  -pthread
  -Wno-unused-local-typedef
  -Wno-unused-variable
  -Wno-missing-field-initializers
  -Wno-unused-function
  -fPIC
  -fpic
  -fno-strict-aliasing
  -fPIE
  -fno-omit-frame-pointer
  -fno-short-enums
  -Wno-deprecated-declarations
  )
#add_link_options(-fuse-ld=lld)
#add_link_options(-Wl,--verbose)
link_libraries(uv)


project(MyProject)

add_executable(foo "a.cpp" 
continuation.cpp
stack_traits.cpp
)

add_executable(b "b.cpp" 
continuation.cpp
stack_traits.cpp
)
add_executable(c "c.cpp" 
continuation.cpp
stack_traits.cpp
)
