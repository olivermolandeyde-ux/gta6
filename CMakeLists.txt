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
    engine/world/WorldStreamer.cpp
    engine/world/TerrainSystem.cpp
    engine/world/SkySystem.cpp
    engine/world/CloudSystem.cpp
    engine/eco/TrafficSystem.cpp
    engine/eco/PowerGridSystem.cpp
    engine/network/NetworkCore.cpp
    engine/network/InterestManagement.cpp
    engine/network/StateReplication.cpp
    engine/audio/AudioEngine.cpp
    engine/eco/WeatherSystem.cpp
    engine/vfx/ParticleSystem.cpp
    engine/gameplay/BallisticsSystem.cpp
    engine/gameplay/WeaponSystem.cpp
    engine/gameplay/VehicleInteraction.cpp
    engine/gameplay/InventorySystem.cpp
    engine/gameplay/PlayerController.cpp
    engine/ui/HUDSystem.cpp
    engine/mission/MissionSystem.cpp
    engine/mission/DialogSystem.cpp
    engine/mission/ObjectiveTracker.cpp
    engine/mission/MissionMarker.cpp
    engine/mission/RewardSystem.cpp
    engine/mission/MissionDataLoader.cpp
    engine/ai/PoliceTacticalAI.cpp
    engine/ai/GangTerritoryAI.cpp
    engine/ai/CivilianRoutineAI.cpp
    engine/ai/AdvancedNavigation.cpp
    engine/ai/AIDataLoader.cpp
    engine/save/WorldSerializer.cpp
    engine/save/WorldDeserializer.cpp
    engine/save/SaveSlotManager.cpp
    engine/multiplayer/GameModeCore.cpp
    engine/multiplayer/DeathmatchMode.cpp
    engine/multiplayer/RaceMode.cpp
    engine/multiplayer/CoopMissionMode.cpp
)

target_include_directories(leonida_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

function(leonida_sandbox name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} PRIVATE leonida_core)
endfunction()

leonida_sandbox(leonida_phase1_sandbox sandbox/phase1_ecs_sandbox.cpp)
leonida_sandbox(leonida_phase2_sandbox sandbox/phase2_render_sandbox.cpp)
leonida_sandbox(leonida_phase3_sandbox sandbox/phase3_objects_sandbox.cpp)
leonida_sandbox(leonida_phase4_sandbox sandbox/phase4_vehicle_sandbox.cpp)
leonida_sandbox(leonida_phase5_sandbox sandbox/phase5_pedestrian_sandbox.cpp)
leonida_sandbox(leonida_phase6_sandbox sandbox/phase6_world_sandbox.cpp)
leonida_sandbox(leonida_phase7_sandbox sandbox/phase7_network_sandbox.cpp)
leonida_sandbox(leonida_phase8_sandbox sandbox/phase8_audio_weather_sandbox.cpp)
leonida_sandbox(leonida_phase11_sandbox sandbox/phase11_gameplay_sandbox.cpp)
leonida_sandbox(leonida_phase12_sandbox sandbox/phase12_mission_sandbox.cpp)
target_compile_definitions(leonida_phase12_sandbox PRIVATE
    LEONIDA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
leonida_sandbox(leonida_phase13_sandbox sandbox/phase13_ai_sandbox.cpp)
target_compile_definitions(leonida_phase13_sandbox PRIVATE
    LEONIDA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
leonida_sandbox(leonida_phase14_sandbox sandbox/phase14_save_sandbox.cpp)
target_compile_definitions(leonida_phase14_sandbox PRIVATE
    LEONIDA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
leonida_sandbox(leonida_phase15_sandbox sandbox/phase15_multiplayer_sandbox.cpp)

add_executable(leonida_engine sandbox/LeonidaEngineMain.cpp)
target_link_libraries(leonida_engine PRIVATE leonida_core)
target_compile_definitions(leonida_engine PRIVATE
    LEONIDA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_executable(leonida_visual
    sandbox/VisualSandbox.cpp
    engine/render/MetalRenderer.cpp
    engine/render/MetalWindow.cpp
    engine/render/InputSystem.cpp
)
target_link_libraries(leonida_visual PRIVATE leonida_core)
target_compile_definitions(leonida_visual PRIVATE
    LEONIDA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
if(APPLE)
    # Compiled as Objective-C++ (same as .mm) so NSEvent/Metal types are legal.
    set_source_files_properties(
        engine/render/MetalRenderer.cpp
        engine/render/MetalWindow.cpp
        sandbox/VisualSandbox.cpp
        sandbox/TerrainSandbox.cpp
        PROPERTIES COMPILE_FLAGS "-x objective-c++"
    )
    target_link_libraries(leonida_visual PRIVATE
        "-framework Metal -framework QuartzCore -framework Cocoa")
endif()

add_executable(leonida_terrain
    sandbox/TerrainSandbox.cpp
    engine/render/MetalRenderer.cpp
    engine/render/MetalWindow.cpp
    engine/render/InputSystem.cpp
)
target_link_libraries(leonida_terrain PRIVATE leonida_core)
target_compile_definitions(leonida_terrain PRIVATE
    LEONIDA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
if(APPLE)
    target_link_libraries(leonida_terrain PRIVATE
        "-framework Metal -framework QuartzCore -framework Cocoa")
endif()

add_executable(leonida_full_sandbox
    sandbox/FullIntegrationSandbox.cpp
)
target_link_libraries(leonida_full_sandbox PRIVATE leonida_core)
target_compile_definitions(leonida_full_sandbox PRIVATE
    LEONIDA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if(UNIX)
    target_link_libraries(leonida_core PUBLIC pthread)
endif()

# ---------------------------------------------------------------------------
# HLSL shader compilation / staging (PBR, SSS, CarPaint, TireRubber)
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
        COMMAND ${DXC_EXECUTABLE} -T ps_6_0 -E PS_Main
                -Fo ${SHADER_OUTPUT_DIR}/MasterPBR.ps.cso
                ${SHADER_SOURCE_DIR}/MasterPBR.hlsl
        DEPENDS ${SHADER_SOURCE_DIR}/MasterPBR.hlsl
        COMMENT "DXC compile MasterPBR.hlsl (ps_6_0 PS_Main)"
    )
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT_DIR}/SkinSSS.ps.cso
        COMMAND ${DXC_EXECUTABLE} -T ps_6_0 -E CalculateSkinShading
                -Fo ${SHADER_OUTPUT_DIR}/SkinSSS.ps.cso
                ${SHADER_SOURCE_DIR}/SkinSSS.hlsl
        DEPENDS ${SHADER_SOURCE_DIR}/SkinSSS.hlsl
        COMMENT "DXC compile SkinSSS.hlsl"
    )
    list(APPEND SHADER_STAGED
        ${SHADER_OUTPUT_DIR}/MasterPBR.ps.cso
        ${SHADER_OUTPUT_DIR}/SkinSSS.ps.cso)
endif()

add_custom_target(leonida_shaders ALL DEPENDS ${SHADER_STAGED})
add_dependencies(leonida_core leonida_shaders)

# ---------------------------------------------------------------------------
# City generator + full sandbox
# ---------------------------------------------------------------------------
set(LEONIDA_PYTHON "")
if(EXISTS "${CMAKE_SOURCE_DIR}/tools/.venv/bin/python")
    set(LEONIDA_PYTHON "${CMAKE_SOURCE_DIR}/tools/.venv/bin/python")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/tools/.venv/Scripts/python.exe")
    set(LEONIDA_PYTHON "${CMAKE_SOURCE_DIR}/tools/.venv/Scripts/python.exe")
else()
    find_program(LEONIDA_PYTHON NAMES python3 python)
endif()

if(LEONIDA_PYTHON)
    add_custom_target(generate_city
        COMMAND ${LEONIDA_PYTHON} ${CMAKE_SOURCE_DIR}/tools/citygen/master_generate.py
                --size-km 2 --seed 42 --coast west
                --out ${CMAKE_SOURCE_DIR}/output/city
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/citygen
        COMMENT "Generate 2 km Leonida city (.cell prefabs)"
    )
    add_custom_target(run_full_sandbox
        DEPENDS leonida_full_sandbox generate_city
        COMMAND $<TARGET_FILE:leonida_full_sandbox>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Run 10 s full-integration sandbox on generated city"
    )
endif()
