cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

find_program(CLANG_FORMAT_EXE NAMES "clang-format" DOC "Path to clang-format executable")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(GPU_PARTICLE_SYSTEM_SOURCES ${PROJECT_SOURCE_DIR}/src/main.cpp
                                ${PROJECT_SOURCE_DIR}/src/imgui_curve_editor.h
                                ${PROJECT_SOURCE_DIR}/src/imgui_curve_editor.cpp
                                ${PROJECT_SOURCE_DIR}/src/imgui_color_gradient.h
                                ${PROJECT_SOURCE_DIR}/src/imgui_color_gradient.cpp)
file(GLOB_RECURSE SHADER_SOURCES ${PROJECT_SOURCE_DIR}/src/*.glsl)

if (APPLE)
    add_executable(GPUParticleSystem MACOSX_BUNDLE ${GPU_PARTICLE_SYSTEM_SOURCES} ${SHADER_SOURCES} ${ASSET_SOURCES})
    set(MACOSX_BUNDLE_BUNDLE_NAME "GPUParticleSystem") 
    set_source_files_properties(${SHADER_SOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/shader)
    set_source_files_properties(${ASSET_SOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
else()
    add_executable(GPUParticleSystem ${GPU_PARTICLE_SYSTEM_SOURCES}) 
endif()

target_link_libraries(GPUParticleSystem dwSampleFramework)

if (NOT APPLE)
    add_custom_command(TARGET GPUParticleSystem POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/src/shader $<TARGET_FILE_DIR:GPUParticleSystem>/shader)
endif()

if(CLANG_FORMAT_EXE)
    add_custom_target(GPUParticleSystem-clang-format COMMAND ${CLANG_FORMAT_EXE} -i -style=file ${GPU_PARTICLE_SYSTEM_SOURCES} ${SHADER_SOURCES})
endif()

set_property(TARGET GPUParticleSystem PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$(Configuration)")