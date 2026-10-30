cmake_minimum_required(VERSION 3.20)

# STLT-VJ: a live video player built around HAP, dockable panels, FFGL effects and slice mapping.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Oldest macOS the app runs on")

project(STLT-VJ VERSION 0.1.0 LANGUAGES C CXX)
# Objective-C for Syphon and the macOS bits of the dependencies: enabled here, before any of them, or a
# dependency enabling it in its own folder first would leave this one without the compile rules.
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

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
# Sound output on every system (public domain / MIT-0): one header, built in src/audio/Miniaudio.c.
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.21
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-cmake)
# MIDI input on every system (MIT). Its sources are built below with the right system API.
FetchContent_Declare(rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG 6.0.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-cmake)

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
            "${FFMPEG_ROOT}/lib/avformat.lib" "${FFMPEG_ROOT}/lib/avcodec.lib" "${FFMPEG_ROOT}/lib/swscale.lib"
            "${FFMPEG_ROOT}/lib/swresample.lib" "${FFMPEG_ROOT}/lib/avutil.lib")
        file(GLOB FFMPEG_DLLS "${FFMPEG_ROOT}/bin/*.dll")
    else()
        find_package(PkgConfig REQUIRED)
        if(FFMPEG_ROOT)
            # Our own static build: link its private dependencies too.
            set(ENV{PKG_CONFIG_PATH} "${FFMPEG_ROOT}/lib/pkgconfig")
            pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libswscale libswresample libavutil)
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
            pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavutil)
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
    src/media/AudioDecoder.cpp
    src/media/AudioDecoder.h
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
    src/Timeline.cpp
    src/Preferences.cpp
    src/Preferences.h
    src/chaser/Chaser.cpp
    src/chaser/Chaser.h
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
    src/output/NdiSender.cpp
    src/output/Sharing.h
    src/control/Control.cpp
    src/control/Control.h
    src/control/RemoteInputs.cpp
    src/control/RemoteInputs.h
    src/audio/AudioEngine.cpp
    src/audio/AudioEngine.h
    src/audio/Miniaudio.c
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

# MIDI through RtMidi: Windows Multimedia, CoreMIDI or ALSA.
FetchContent_MakeAvailable(rtmidi)
add_library(rtmidi STATIC "${rtmidi_SOURCE_DIR}/RtMidi.cpp")
target_include_directories(rtmidi PUBLIC "${rtmidi_SOURCE_DIR}")
if(WIN32)
    target_compile_definitions(rtmidi PRIVATE __WINDOWS_MM__)
    target_link_libraries(rtmidi PUBLIC winmm)
elseif(APPLE)
    target_compile_definitions(rtmidi PRIVATE __MACOSX_CORE__)
    target_link_libraries(rtmidi PUBLIC "-framework CoreMIDI" "-framework CoreAudio" "-framework CoreFoundation")
else()
    find_package(ALSA)
    find_package(Threads REQUIRED)
    if(ALSA_FOUND)
        target_compile_definitions(rtmidi PRIVATE __LINUX_ALSA__)
        target_link_libraries(rtmidi PUBLIC ALSA::ALSA Threads::Threads)
    else()
        message(WARNING "ALSA development files not found: STLT-VJ is built without MIDI input")
        target_compile_definitions(rtmidi PRIVATE __RTMIDI_DUMMY__)
    endif()
endif()
target_link_libraries(stltvj-core PUBLIC rtmidi)

# Sound through miniaudio, which finds the system's audio libraries at run time.
FetchContent_MakeAvailable(miniaudio)
set_source_files_properties(src/audio/Miniaudio.c PROPERTIES COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/W0,-w>")
target_include_directories(stltvj-core PUBLIC "${miniaudio_SOURCE_DIR}")
if(APPLE)
    target_link_libraries(stltvj-core PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(stltvj-core PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
if(WIN32)
    target_link_libraries(stltvj-core PUBLIC ws2_32) # OSC over UDP
endif()

# Screens shared with other programs on the same computer: Spout on Windows, Syphon on macOS, both BSD-licensed
# and built from their sources here. (NDI needs nothing at build time: its runtime is loaded when used.)
if(WIN32)
    FetchContent_Declare(spout2
        GIT_REPOSITORY https://github.com/leadedge/Spout2.git
        GIT_TAG 2.007.017
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR no-cmake) # only the SpoutGL sources, built below with this project's runtime settings
    FetchContent_MakeAvailable(spout2)
    set(SPOUT_GL "${spout2_SOURCE_DIR}/SPOUTSDK/SpoutGL")
    add_library(spout STATIC
        ${SPOUT_GL}/Spout.cpp ${SPOUT_GL}/SpoutCopy.cpp ${SPOUT_GL}/SpoutDirectX.cpp ${SPOUT_GL}/SpoutFrameCount.cpp
        ${SPOUT_GL}/SpoutGL.cpp ${SPOUT_GL}/SpoutGLextensions.cpp ${SPOUT_GL}/SpoutSender.cpp ${SPOUT_GL}/SpoutReceiver.cpp
        ${SPOUT_GL}/SpoutSenderNames.cpp ${SPOUT_GL}/SpoutSharedMemory.cpp ${SPOUT_GL}/SpoutUtils.cpp)
    target_include_directories(spout PUBLIC "${SPOUT_GL}")
    target_compile_options(spout PRIVATE /W0)
    target_link_libraries(spout PUBLIC opengl32 d3d9 d3d11 dxgi version winmm comctl32 shlwapi advapi32 user32 gdi32 shell32 ole32)
    target_sources(stltvj-core PRIVATE src/output/SpoutSharing.cpp)
    target_link_libraries(stltvj-core PUBLIC spout)
elseif(APPLE)
    FetchContent_Declare(syphon
        GIT_REPOSITORY https://github.com/Syphon/Syphon-Framework.git
        # Syphon 5 from its main branch (the tag named 5 is an older tree, without SyphonOpenGLServer and ARC).
        GIT_TAG f4761677a45b8034a3c2069ec0f3d2553da81fba
        SOURCE_SUBDIR no-cmake) # an Xcode project: its OpenGL sources are built below
    FetchContent_MakeAvailable(syphon)
    file(GLOB SYPHON_SOURCES "${syphon_SOURCE_DIR}/*.m" "${syphon_SOURCE_DIR}/*.c")
    list(FILTER SYPHON_SOURCES EXCLUDE REGEX "Metal")
    add_library(syphon STATIC ${SYPHON_SOURCES})
    # Its headers import <Syphon/...>, as from the framework.
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/syphon-include")
    file(CREATE_LINK "${syphon_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/syphon-include/Syphon" SYMBOLIC)
    target_include_directories(syphon PUBLIC "${CMAKE_BINARY_DIR}/syphon-include" "${syphon_SOURCE_DIR}")
    target_compile_definitions(syphon PUBLIC SYPHON_CORE_SHARE PRIVATE GL_SILENCE_DEPRECATION)
    target_compile_options(syphon PRIVATE -fobjc-arc -include "${syphon_SOURCE_DIR}/Syphon_Prefix.pch" -w)
    target_link_libraries(syphon PUBLIC "-framework Cocoa" "-framework IOSurface" "-framework OpenGL")
    target_sources(stltvj-core PRIVATE src/output/SyphonSharing.mm)
    set_source_files_properties(src/output/SyphonSharing.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(stltvj-core PUBLIC syphon)
else()
    target_sources(stltvj-core PRIVATE src/output/NoLocalSharing.cpp)
endif()

# The interface fonts (Inter and JetBrains Mono, SIL Open Font License) are compiled into the program.
set(STLTVJ_FONTS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inter-Regular.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inter-SemiBold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/JetBrainsMono-Medium.ttf")
set(STLTVJ_FONT_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/generated/EmbeddedFonts.cpp")
add_custom_command(OUTPUT "${STLTVJ_FONT_SOURCE}"
    COMMAND ${CMAKE_COMMAND} "-DOUTPUT=${STLTVJ_FONT_SOURCE}" "-DFONTS=${STLTVJ_FONTS}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFonts.cmake"
    DEPENDS ${STLTVJ_FONTS} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedFonts.cmake"
    COMMENT "Embedding the interface fonts"
    VERBATIM)

add_executable(stlt-vj WIN32 MACOSX_BUNDLE
    "${STLTVJ_FONT_SOURCE}"
    src/main.cpp
    src/App.cpp
    src/App.h
    src/ui/AdvancedOutputUi.cpp
    src/ui/AdvancedOutputUi.h
    src/ui/ChaserUi.cpp
    src/ui/ChaserUi.h
    src/ui/ControlUi.cpp
    src/ui/ControlUi.h
    src/ui/EffectsUi.cpp
    src/ui/EffectsUi.h
    src/ui/FileDialogs.cpp
    src/ui/FileDialogs.h
    src/ui/Panels.cpp
    src/ui/Panels.h
    src/ui/PreferencesUi.cpp
    src/ui/PreferencesUi.h
    src/ui/Theme.cpp
    src/ui/Theme.h
    src/ui/TimelineUi.cpp
    src/ui/TimelineUi.h
    src/ui/Widgets.cpp
    src/ui/Widgets.h
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

# Remote control: addresses, OSC packets, MIDI messages, settings.
add_executable(control_test tests/control_test.cpp)
target_link_libraries(control_test PRIVATE stltvj-core)
add_test(NAME control_test COMMAND control_test)
stltvj_ffmpeg_runtime(control_test)

# Clip sound: decoding, seeking and the mix, without an audio device.
add_executable(audio_test tests/audio_test.cpp)
target_link_libraries(audio_test PRIVATE stltvj-core)
target_compile_definitions(audio_test PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME audio_test COMMAND audio_test)
stltvj_ffmpeg_runtime(audio_test)

# Clip ranges, cue points, BPM sync, beat animations, transitions.
add_executable(clip_test tests/clip_test.cpp)
target_link_libraries(clip_test PRIVATE stltvj-core)
target_compile_definitions(clip_test PRIVATE FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
add_test(NAME clip_test COMMAND clip_test)
stltvj_ffmpeg_runtime(clip_test)

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
