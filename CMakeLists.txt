cmake_minimum_required(VERSION 3.20)
project(graph_editor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.4
	GIT_SHALLOW TRUE
)
FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG docking
	GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw imgui)

add_library(
	imgui STATIC
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(
	imgui
	PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw)

add_library(imnodes STATIC third-party/imnodes/imnodes.cpp)
target_include_directories(imnodes PUBLIC third-party/imnodes)
target_link_libraries(imnodes PUBLIC imgui)

add_executable(
	graph_editor
	src/main.cpp
	src/rendergraph.cpp
	src/rendergraph-demo.cpp
)
target_link_libraries(graph_editor PRIVATE imnodes "-framework OpenGL")
target_compile_definitions(graph_editor PRIVATE GL_SILENCE_DEPRECATION)
