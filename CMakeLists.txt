cmake_minimum_required(VERSION 3.20)
project(leonida_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:preprocessor /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

add_library(leonida_core STATIC
    engine/core/Assert.cpp
    engine/memory/PageAllocator.cpp
    engine/memory/LinearAllocator.cpp
    engine/memory/PoolAllocator.cpp
    engine/memory/StackAllocator.cpp
    engine/memory/FrameAllocator.cpp
    engine/memory/MemorySystem.cpp
    engine/ecs/ComponentRegistry.cpp
    engine/ecs/Chunk.cpp
    engine/ecs/Archetype.cpp
    engine/ecs/CommandBuffer.cpp
    engine/ecs/World.cpp
    engine/ecs/SystemScheduler.cpp
)

target_include_directories(leonida_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

add_executable(leonida_phase1_sandbox
    sandbox/phase1_ecs_sandbox.cpp
)

target_link_libraries(leonida_phase1_sandbox PRIVATE leonida_core)

if(UNIX)
    target_link_libraries(leonida_core PUBLIC pthread)
endif()
