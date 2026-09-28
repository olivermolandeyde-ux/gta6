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
    engine/ecs/Query.cpp
    engine/ecs/World.cpp
    engine/ecs/SystemScheduler.cpp
    engine/render/RenderPipeline.cpp
    engine/objects/VehicleEngine.cpp
    engine/objects/BreakableWindow.cpp
    engine/objects/StreetLight.cpp
    engine/physics/VehicleDynamics.cpp
    engine/ai/PedestrianBehavior.cpp
    engine/animation/TwoBoneIK.cpp
    engine/animation/AnimationStateMachine.cpp
)

target_include_directories(leonida_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

add_executable(leonida_phase1_sandbox
    sandbox/phase1_ecs_sandbox.cpp
)
target_link_libraries(leonida_phase1_sandbox PRIVATE leonida_core)

add_executable(leonida_phase2_sandbox
    sandbox/phase2_render_sandbox.cpp
)
target_link_libraries(leonida_phase2_sandbox PRIVATE leonida_core)

add_executable(leonida_phase3_sandbox
    sandbox/phase3_objects_sandbox.cpp
)
target_link_libraries(leonida_phase3_sandbox PRIVATE leonida_core)

add_executable(leonida_phase4_sandbox
    sandbox/phase4_vehicle_sandbox.cpp
)
target_link_libraries(leonida_phase4_sandbox PRIVATE leonida_core)

add_executable(leonida_phase5_sandbox
    sandbox/phase5_pedestrian_sandbox.cpp
)
target_link_libraries(leonida_phase5_sandbox PRIVATE leonida_core)

if(UNIX)
    target_link_libraries(leonida_core PUBLIC pthread)
endif()

# ---------------------------------------------------------------------------
# HLSL shader compilation / staging
# ---------------------------------------------------------------------------
set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

set(SHADER_SOURCES
    ${SHADER_SOURCE_DIR}/MasterPBR.hlsl
    ${SHADER_SOURCE_DIR}/SkinSSS.hlsl
    ${SHADER_SOURCE_DIR}/CarPaintClearcoat.hlsl
    ${SHADER_SOURCE_DIR}/TireRubber.hlsl
)

set(SHADER_STAGED)
foreach(SHADER_FILE ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
    set(STAGED ${SHADER_OUTPUT_DIR}/${SHADER_NAME})
    add_custom_command(
        OUTPUT ${STAGED}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_FILE} ${STAGED}
        DEPENDS ${SHADER_FILE}
        COMMENT "Stage shader ${SHADER_NAME}"
    )
    list(APPEND SHADER_STAGED ${STAGED})
endforeach()

find_program(DXC_EXECUTABLE NAMES dxc dxc.exe)
if(DXC_EXECUTABLE)
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT_DIR}/MasterPBR.ps.cso
        COMMAND ${DXC_EXECUTABLE}
                -T ps_6_0
                -E PS_Main
                -Fo ${SHADER_OUTPUT_DIR}/MasterPBR.ps.cso
                ${SHADER_SOURCE_DIR}/MasterPBR.hlsl
        DEPENDS ${SHADER_SOURCE_DIR}/MasterPBR.hlsl
        COMMENT "DXC compile MasterPBR.hlsl (ps_6_0 PS_Main)"
    )
    list(APPEND SHADER_STAGED ${SHADER_OUTPUT_DIR}/MasterPBR.ps.cso)
endif()

add_custom_target(leonida_shaders ALL DEPENDS ${SHADER_STAGED})
add_dependencies(leonida_core leonida_shaders)
