cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

add_executable(GPUParticleSystem "main.cpp") 

target_link_libraries(GPUParticleSystem dwSampleFramework)

add_custom_command(TARGET GPUParticleSystem POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/src/shader $<TARGET_FILE_DIR:ProgressivePathTracer>/shader)
add_custom_command(TARGET GPUParticleSystem POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/src/shader ${CMAKE_SOURCE_DIR}/build/src/shader)
