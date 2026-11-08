# OS999 for folding phones: iOS app and Android native library.
#
# iOS (from the repository root, after sh mobile/scripts/fetch-deps.sh):
#   cmake -S mobile -B build/mobile/ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
#         -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0
#   open build/mobile/ios/OS999Mobile.xcodeproj      (or: sh mobile/scripts/ios-sim.sh)
# Android: mobile/android builds this file through Gradle's externalNativeBuild.
cmake_minimum_required(VERSION 3.22)
project(OS999Mobile C)
if(APPLE)
    enable_language(OBJC)
endif()

set(OS999_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
get_filename_component(OS999_ROOT ${OS999_ROOT} ABSOLUTE)
set(OS999_KEYBOARD ${OS999_ROOT}/keyboard CACHE PATH "OS999 keyboard sources")
set(OS999_DEPS ${OS999_ROOT}/build/deps CACHE PATH "Fetched SDL2 and FreeType sources")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT EXISTS ${OS999_DEPS}/SDL2/CMakeLists.txt)
    message(FATAL_ERROR "SDL2 source missing: run sh mobile/scripts/fetch-deps.sh from the repository root")
endif()
if(ANDROID)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)    # SDLActivity loads libSDL2.so
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${OS999_DEPS}/SDL2 ${CMAKE_BINARY_DIR}/SDL2 EXCLUDE_FROM_ALL)

set(OS999_FREETYPE "")
if(EXISTS ${OS999_DEPS}/freetype/CMakeLists.txt)
    foreach(opt FT_DISABLE_ZLIB FT_DISABLE_BZIP2 FT_DISABLE_PNG FT_DISABLE_HARFBUZZ FT_DISABLE_BROTLI
                FT_DISABLE_HVF)
        set(${opt} ON CACHE BOOL "" FORCE)
    endforeach()
    add_subdirectory(${OS999_DEPS}/freetype ${CMAKE_BINARY_DIR}/freetype EXCLUDE_FROM_ALL)
    set(OS999_FREETYPE freetype)
endif()

# Consume the same core manifest as desktop and headless mobile builds.
file(READ "${OS999_ROOT}/config/core-sources.mk" CORE_MANIFEST)
string(REGEX MATCHALL "(src|assets)/[A-Za-z0-9_/.-]+\\.c" CORE_FILES "${CORE_MANIFEST}")
set(SOURCES "")
foreach(f ${CORE_FILES})
    list(APPEND SOURCES ${OS999_ROOT}/${f})
endforeach()
foreach(f quicktime_decode quicktime_movie quicktime_backend media_player_panel winamp_skin os999_startup desktop_host desktop_sound audio_decoder audio_dsp audio_midi audio_output audio_service)
    list(APPEND SOURCES ${OS999_ROOT}/src/${f}.c)
endforeach()
# These must be target-platform dependencies when cross-compiling.
include(${CMAKE_CURRENT_SOURCE_DIR}/dependencies.cmake)
set(KBD kbd_core kbd_keys kbd_layout kbd_text kbd_touch kbd_pad kbd_motion kbd_anim kbd_gfx
    kbd_skin kbd_draw kbd_draw_over kbd_synth kbd_mixer kbd_sdl kbd_haptics)
foreach(f ${KBD})
    list(APPEND SOURCES ${OS999_KEYBOARD}/src/${f}.c)
endforeach()
file(GLOB MOBILE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
list(APPEND SOURCES ${MOBILE_SOURCES}
     ${CMAKE_CURRENT_SOURCE_DIR}/sdl/mobile_main.c ${CMAKE_CURRENT_SOURCE_DIR}/sdl/mobile_platform.c)

set(INCLUDES ${OS999_ROOT}/include ${OS999_ROOT}/storage/include ${OS999_ROOT}/assets/icons/c ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/sdl ${OS999_KEYBOARD}/include ${OS999_KEYBOARD}/src)

if(ANDROID)
    add_library(main SHARED ${SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/sdl/mobile_compat_android.c)
    set(APP main)
    target_link_libraries(main PRIVATE SDL2::SDL2 ${OS999_FREETYPE} android log m)
    # Bionic lacks shm_open/shm_unlink; declare the file-backed versions everywhere.
    target_compile_options(main PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/sdl/mobile_compat_android.h)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(IOS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ios)
    set(IOS_RES ${IOS_DIR}/Assets.xcassets ${IOS_DIR}/LaunchScreen.storyboard)
    add_executable(OS999 MACOSX_BUNDLE ${SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/sdl/mobile_ios.m ${IOS_RES})
    set(APP OS999)
    set_source_files_properties(${IOS_RES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_link_libraries(OS999 PRIVATE SDL2::SDL2main SDL2::SDL2-static ${OS999_FREETYPE}
        "-framework UIKit" "-framework AVFoundation" "-framework CoreHaptics" "-framework GameController")
    set_target_properties(OS999 PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${IOS_DIR}/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER org.remnantsecurity.os999.mobile
        MACOSX_BUNDLE_BUNDLE_NAME OS999
        MACOSX_BUNDLE_SHORT_VERSION_STRING 0.1
        MACOSX_BUNDLE_BUNDLE_VERSION 1
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER org.remnantsecurity.os999.mobile
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME AppIcon
        XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Automatic)
    # Desktop data the shell reads by relative path, copied flat into the bundle.
    add_custom_command(TARGET OS999 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -DROOT=${OS999_ROOT} -DDEST=$<TARGET_BUNDLE_DIR:OS999>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/copy-resources.cmake)
else()
    message(FATAL_ERROR "Use mobile/mobile.mk for the desk simulator; this file builds iOS and Android")
endif()

target_include_directories(${APP} PRIVATE ${INCLUDES})
target_compile_definitions(${APP} PRIVATE _DEFAULT_SOURCE)
if(OS999_FREETYPE)
    target_compile_definitions(${APP} PRIVATE OSPI_FREETYPE)
endif()

target_link_libraries(${APP} PRIVATE os999_mobile_dependencies)
