cmake_minimum_required(VERSION 3.21)

# The project name is the name of the SKSE .dll, and the name SKSE reports for
# the plugin - which is also what the log file is named after.
project(VRNPCEditor VERSION 0.1.0 LANGUAGES CXX)

# directxtk must be found before CommonLibSSE: CommonLibSSE names
# Microsoft::DirectXTK in its link interface and CMake resolves imported
# targets eagerly.
find_package(directxtk CONFIG REQUIRED)
find_package(CommonLibSSE CONFIG REQUIRED)

include(cmake/headerlist.cmake)
include(cmake/sourcelist.cmake)

# No COMPATIBLE_RUNTIMES here - the helper parses that list as version numbers.
# Runtime differences are gated on REL::Module::IsVR() in code instead.
add_commonlibsse_plugin(${PROJECT_NAME} AUTHOR "bellbound" SOURCES ${headers} ${sources})

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

target_include_directories(
	"${PROJECT_NAME}"
	PRIVATE
		${SIMPLEINI_INCLUDE_DIRS}
		${CMAKE_SOURCE_DIR}/external
		${CMAKE_SOURCE_DIR}/src
)

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

# The build script never applies a CMakePreset, so set these on the target.
target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Release still needs a PDB - crash logs are unreadable without one.
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Release>:/Zi>")
    target_link_options(${PROJECT_NAME} PRIVATE "$<$<CONFIG:Release>:/DEBUG>" "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
endif()
