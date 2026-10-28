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
option(STLTVJ_WITH_FFMPEG "Play files that are not HAP (H.264, ProRes, DXV, PNG, JPEG...) through FFmpeg" ON)
set(FFMPEG_ROOT "" CACHE PATH "FFmpeg install prefix (app/scripts/build-ffmpeg.sh); empty: system FFmpeg, or a download on Windows")

include(FetchContent)
enable_testing()

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
FetchContent_Declare(nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG v1.2.1
    GIT_SHALLOW TRUE)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE) # Linux is a development target only; X11 is enough
set(SNAPPY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SNAPPY_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(SNAPPY_INSTALL OFF CACHE BOOL "" FORCE)
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "" FORCE)

# imgui and hap have no CMake project of their own: MakeAvailable only downloads them.
FetchContent_MakeAvailable(snappy hap)
if(STLTVJ_BUILD_APP)
    FetchContent_MakeAvailable(glfw imgui nfd)
endif()

# FFmpeg decodes everything that is not HAP.
if(STLTVJ_WITH_FFMPEG)
    add_library(ffmpeg INTERFACE)
    if(WIN32)
        if(NOT FFMPEG_ROOT)
            # Shared FFmpeg 7.1 build for Windows (GPL, gyan.dev); its DLLs are copied next to the executables.
            FetchContent_Declare(ffmpeg_windows
                URL https://github.com/GyanD/codexffmpeg/releases/download/7.1/ffmpeg-7.1-full_build-shared.zip
                URL_HASH SHA256=fedf38dbdd00fa189da06afb0abba69a19312412e05fa2e8e89b3c06aafebcdc)
            FetchContent_MakeAvailable(ffmpeg_windows)
            set(FFMPEG_ROOT "${ffmpeg_windows_SOURCE_DIR}")
        endif()
        target_include_directories(ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
        target_link_libraries(ffmpeg INTERFACE
            "${FFMPEG_ROOT}/lib/avformat.lib" "${FFMPEG_ROOT}/lib/avcodec.lib" "${FFMPEG_ROOT}/lib/swscale.lib" "${FFMPEG_ROOT}/lib/avutil.lib")
        file(GLOB FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")
    else()
        find_package(PkgConfig REQUIRED)
        if(FFMPEG_ROOT)
            # Our own static build: link its private dependencies too.
            set(ENV{PKG_CONFIG_PATH} "${FFMPEG_ROOT}/lib/pkgconfig")
            pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswscale libavutil)
            target_include_directories(ffmpeg INTERFACE ${FFMPEG_STATIC_INCLUDE_DIRS})
            target_link_directories(ffmpeg INTERFACE ${FFMPEG_STATIC_LIBRARY_DIRS})
            # pkg-config splits "-framework CoreVideo" in two; joined again, CMake passes it to the linker whole
            # (apart, CoreVideo would become -lCoreVideo).
            set(FFMPEG_LINK "")
            set(FRAMEWORK_NEXT FALSE)
            foreach(flag IN LISTS FFMPEG_STATIC_LDFLAGS)
                if(FRAMEWORK_NEXT)
                    list(APPEND FFMPEG_LINK "-framework ${flag}")
                    set(FRAMEWORK_NEXT FALSE)
                elseif(flag STREQUAL "-framework")
                    set(FRAMEWORK_NEXT TRUE)
                else()
                    list(APPEND FFMPEG_LINK "${flag}")
                endif()
            endforeach()
            target_link_libraries(ffmpeg INTERFACE ${FFMPEG_LINK})
        else()
            pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
            target_link_libraries(ffmpeg INTERFACE PkgConfig::FFMPEG)
        endif()
    endif()
endif()

# Copies the FFmpeg DLLs next to an executable on Windows.
function(stltvj_ffmpeg_runtime target)
    if(WIN32 AND STLTVJ_WITH_FFMPEG AND FFMPEG_DLLS)
        add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_DLLS} $<TARGET_FILE_DIR:${target}>)
    endif()
endfunction()

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
    src/media/FfmpegSource.cpp
    src/media/FileReader.cpp
    src/media/FileReader.h
    src/media/FrameSource.h
    src/media/HapFrame.cpp
    src/media/HapFrame.h
    src/media/HapSource.cpp
    src/media/HapSource.h
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
if(STLTVJ_WITH_FFMPEG)
    target_compile_definitions(stltvj-media PUBLIC STLTVJ_WITH_FFMPEG)
    target_link_libraries(stltvj-media PUBLIC ffmpeg)
endif()

if(STLTVJ_BUILD_APP)
# Everything but the user interface: composition model, effects, FFGL host, rendering.
add_library(stltvj-core STATIC
    src/Composition.cpp
    src/Composition.h
    src/History.cpp
    src/History.h
    src/Settings.cpp
    src/Settings.h
    src/fx/BuiltinEffects.cpp
    src/fx/BuiltinEffects.h
    src/fx/Effect.cpp
    src/fx/Effect.h
    src/fx/EffectRegistry.cpp
    src/fx/EffectRegistry.h
    src/fx/FfglAbi.h
    src/fx/FfglPlugin.cpp
    src/fx/FfglPlugin.h
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
    src/output/AdvancedOutput.cpp
    src/output/AdvancedOutput.h
    src/output/OutputRenderer.cpp
    src/output/OutputRenderer.h
)
target_include_directories(stltvj-core PUBLIC src third_party/khronos "${THIRD_PARTY}/nlohmann")
target_compile_definitions(stltvj-core PUBLIC GLFW_INCLUDE_NONE)
target_link_libraries(stltvj-core PUBLIC stltvj-media glfw)
if(WIN32)
    target_compile_definitions(stltvj-core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_compile_options(stltvj-core PUBLIC /utf-8)
    target_link_libraries(stltvj-core PUBLIC opengl32 shell32 ole32)
elseif(APPLE)
    target_compile_definitions(stltvj-core PUBLIC GL_SILENCE_DEPRECATION)
    target_link_libraries(stltvj-core PUBLIC "-framework OpenGL" "-framework Cocoa" "-framework IOKit")
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(stltvj-core PUBLIC OpenGL::GL ${CMAKE_DL_LIBS})
endif()

add_executable(stlt-vj WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/App.cpp
    src/App.h
    src/ui/AdvancedOutputUi.cpp
    src/ui/AdvancedOutputUi.h
    src/ui/EffectsUi.cpp
    src/ui/EffectsUi.h
    src/ui/FileDialogs.cpp
    src/ui/FileDialogs.h
    src/ui/Panels.cpp
    src/ui/Panels.h
    src/ui/Theme.cpp
    src/ui/Theme.h
    src/ui/Workspaces.cpp
    src/ui/Workspaces.h
)
target_link_libraries(stlt-vj PRIVATE stltvj-core imgui nfd)
stltvj_ffmpeg_runtime(stlt-vj)
if(WIN32)
    set_target_properties(stlt-vj PROPERTIES LINK_FLAGS "/ENTRY:mainCRTStartup")
endif()

# Effects, parameters and composition files, without a GPU.
add_executable(fx_test tests/fx_test.cpp)
target_link_libraries(fx_test PRIVATE stltvj-core)
add_test(NAME fx_test COMMAND fx_test)
stltvj_ffmpeg_runtime(fx_test)

# Slice geometry, setups and Resolume import, without a GPU.
add_executable(output_test tests/output_test.cpp)
target_link_libraries(output_test PRIVATE stltvj-core)
add_test(NAME output_test COMMAND output_test)
stltvj_ffmpeg_runtime(output_test)

# Undo and redo.
add_executable(history_test tests/history_test.cpp)
target_link_libraries(history_test PRIVATE stltvj-core)
add_test(NAME history_test COMMAND history_test)
stltvj_ffmpeg_runtime(history_test)
endif()

# Headless tools: decode benchmark and tests for the media engine.
add_executable(decodebench tools/decodebench.cpp)
target_link_libraries(decodebench PRIVATE stltvj-media)
stltvj_ffmpeg_runtime(decodebench)

add_executable(media_test tests/media_test.cpp)
target_link_libraries(media_test PRIVATE stltvj-media)
target_compile_definitions(media_test PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME media_test COMMAND media_test)
stltvj_ffmpeg_runtime(media_test)
