project(RoguelikeEngine)

cmake_minimum_required(VERSION 3.9.0)

##### COMPILER SETTINGS #####
set (CMAKE_CXX_STANDARD 17)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

##### OUTPUT DIRECTORIES #####
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

##### OPTIONS #####
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build the GLFW example programs")
set(GLFW_BUILD_TESTS OFF CACHE BOOL "Build the GLFW test programs")
set(GLFW_BUILD_DOCS OFF CACHE BOOL "Build the GLFW documentation")

set(BUILD_STATIC_LIB ON)
set(NO_EXPORT ON)

##### DEPENDENCIES #####
add_subdirectory(deps/assimp)

##### STRUCTURE DEPENDENCIES #####
set_target_properties(zlib PROPERTIES FOLDER "Dependencies/")
set_target_properties(uninstall PROPERTIES FOLDER "Dependencies/")
set_target_properties(assimp PROPERTIES FOLDER "Dependencies/Assimp")
set_target_properties(assimp_cmd PROPERTIES FOLDER "Dependencies/Assimp")
set_target_properties(IrrXML PROPERTIES FOLDER "Dependencies/Assimp")
set_target_properties(UpdateAssimpLibsDebugSymbolsAndDLLs PROPERTIES FOLDER "Dependencies/Assimp")
set_target_properties(zlibstatic PROPERTIES FOLDER "Dependencies/Assimp")
set_target_properties(unit PROPERTIES FOLDER "Dependencies/Assimp")

##### INCLUDE/LINK DIRECTORIES #####
include_directories(
	deps/assimp/include/
	${CMAKE_BINARY_DIR}/deps/assimp/include
)

##### Application #####
project(Benchmark_ConstantBuffers)

file(GLOB SOURCES "src/*.cpp")
file(GLOB HEADERS "src/*.hpp")

add_executable(Benchmark_ConstantBuffers WIN32 ${SOURCES} ${HEADERS})
target_link_libraries(Benchmark_ConstantBuffers dxguid.lib d3d12.lib dxgi.lib d3dcompiler.lib assimp)
set_target_properties(Benchmark_ConstantBuffers PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/../")

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Benchmark_ConstantBuffers)
