cmake_minimum_required(VERSION 3.20)

project(shared_memory_ipc
    VERSION 0.1.0
    DESCRIPTION "Shared-memory IPC using an SPSC ring buffer"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# Header-only code and dependencies shared by both programs.
add_library(ipc_common INTERFACE)

target_compile_features(ipc_common INTERFACE cxx_std_20)

target_include_directories(ipc_common INTERFACE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(ipc_common INTERFACE
    Threads::Threads
)

# shm_open() was provided by librt on older Linux systems. Modern glibc puts it
# in libc, so only link librt when CMake finds it.
find_library(RT_LIBRARY rt)
if(RT_LIBRARY)
    target_link_libraries(ipc_common INTERFACE ${RT_LIBRARY})
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(ipc_common INTERFACE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_executable(test_ipc test/test_ipc.cpp)
target_link_libraries(test_ipc PRIVATE ipc_common)
