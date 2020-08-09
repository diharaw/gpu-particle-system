cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

project("GPUParticleSystem")

IF(APPLE)
	set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++14")
	set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++")
ENDIF()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_subdirectory(external/dwSampleFramework)

include_directories("${DW_SAMPLE_FRAMEWORK_INCLUDES}"
					"${CMAKE_SOURCE_DIR}/external/dwSampleFramework/extras"
					"${CMAKE_SOURCE_DIR}/external/ImGuizmo")

add_subdirectory(src)