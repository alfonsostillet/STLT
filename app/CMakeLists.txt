cmake_minimum_required(VERSION 3.20)

# STLT-VJ: a live video player built around HAP, dockable panels, FFGL effects and slice mapping.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Oldest macOS the app runs on")

project(STLT-VJ VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Snappy 1.2.1 still declares an old minimum CMake version.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

option(STLTVJ_BUILD_APP "Build the app itself (off: only the media engine, tools and tests)" ON)

include(FetchContent)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b-docking
    GIT_SHALLOW TRUE)
FetchContent_Declare(snappy
    GIT_REPOSITORY https://github.com/google/snappy.git
    GIT_TAG 1.2.1
    GIT_SHALLOW TRUE)
FetchContent_Declare(hap
    GIT_REPOSITORY https://github.com/Vidvox/hap.git
    GIT_TAG d847f6bbd3be88575dd4ef33a877243780e3be76)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE) # Linux is a development target only; X11 is enough
set(SNAPPY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SNAPPY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SNAPPY_INSTALL OFF CACHE BOOL "" FORCE)

# imgui and hap have no CMake project of their own: MakeAvailable only downloads them.
FetchContent_MakeAvailable(snappy hap)
if(STLTVJ_BUILD_APP)
    FetchContent_MakeAvailable(glfw imgui)
endif()

set(THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/../third_party")

# Vidvox's reference HAP decoder; we hand it our own thread pool for chunked frames.
add_library(hap STATIC "${hap_SOURCE_DIR}/source/hap.c")
target_include_directories(hap PUBLIC "${hap_SOURCE_DIR}/source")
target_link_libraries(hap PUBLIC snappy)

if(STLTVJ_BUILD_APP)
add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
    "${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp"
)
target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends" "${imgui_SOURCE_DIR}/misc/cpp")
target_link_libraries(imgui PUBLIC glfw)
endif()

# The media engine: demuxing, HAP decoding and timing. No OpenGL, so tools and tests can use it headless.
add_library(stltvj-media STATIC
    src/media/FileReader.cpp
    src/media/FileReader.h
    src/media/HapFrame.cpp
    src/media/HapFrame.h
    src/media/MovFile.cpp
    src/media/MovFile.h
    src/media/ThreadPool.cpp
    src/media/ThreadPool.h
)
target_include_directories(stltvj-media PUBLIC src)
target_link_libraries(stltvj-media PUBLIC hap)
find_package(Threads REQUIRED)
target_link_libraries(stltvj-media PUBLIC Threads::Threads)
if(WIN32)
    target_compile_definitions(stltvj-media PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(STLTVJ_BUILD_APP)
add_executable(stlt-vj WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/App.cpp
    src/App.h
    src/Composition.cpp
    src/Composition.h
    src/Settings.cpp
    src/Settings.h
    src/gl/Gl.cpp
    src/gl/Gl.h
    src/gl/Program.cpp
    src/gl/Program.h
    src/render/ClipPlayer.cpp
    src/render/ClipPlayer.h
    src/render/Compositor.cpp
    src/render/Compositor.h
    src/render/Fbo.cpp
    src/render/Fbo.h
    src/render/OutputWindow.cpp
    src/render/OutputWindow.h
    src/ui/Panels.cpp
    src/ui/Panels.h
    src/ui/Theme.cpp
    src/ui/Theme.h
    src/ui/Workspaces.cpp
    src/ui/Workspaces.h
)
target_include_directories(stlt-vj PRIVATE src third_party/khronos "${THIRD_PARTY}/nlohmann")
target_compile_definitions(stlt-vj PRIVATE GLFW_INCLUDE_NONE)
target_link_libraries(stlt-vj PRIVATE stltvj-media imgui glfw)
if(WIN32)
    target_compile_definitions(stlt-vj PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(stlt-vj PRIVATE /utf-8)
    target_link_libraries(stlt-vj PRIVATE opengl32)
    set_target_properties(stlt-vj PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
elseif(APPLE)
    target_compile_definitions(stlt-vj PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(stlt-vj PRIVATE "-framework OpenGL" "-framework Cocoa" "-framework IOKit")
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(stlt-vj PRIVATE OpenGL::GL ${CMAKE_DL_LIBS})
endif()
endif()

# Headless tools: decode benchmark and tests for the media engine.
add_executable(hapbench tools/hapbench.cpp)
target_link_libraries(hapbench PRIVATE stltvj-media)

enable_testing()
add_executable(media_test tests/media_test.cpp)
target_link_libraries(media_test PRIVATE stltvj-media)
target_compile_definitions(media_test PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME media_test COMMAND media_test)
