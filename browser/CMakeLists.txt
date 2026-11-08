# OS999 Browser: host-native Chromium (CEF) helper for the OS999 desktop.
# Configure from the repository root:
#   cmake -S browser -B build/browser -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake_minimum_required(VERSION 3.21)
project(os999_browser)
option(OS999_ENABLE_TEST_HOOKS "Enable developer-only automation and overrides" OFF)
if(OS999_ENABLE_TEST_HOOKS)
  add_compile_definitions(OS999_ENABLE_TEST_HOOKS=1)
endif()
find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(GLOB_RECURSE OS999_WEB_RESOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/*")
set(WEB_RESOURCES_CC "${CMAKE_CURRENT_BINARY_DIR}/web_app_resources.cc")
add_custom_command(OUTPUT "${WEB_RESOURCES_CC}"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed-web-resources.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/resources" "${WEB_RESOURCES_CC}"
  DEPENDS ${OS999_WEB_RESOURCES} scripts/embed-web-resources.py VERBATIM)
set_property(GLOBAL PROPERTY OS_FOLDERS ON)

set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/cef" CACHE PATH "CEF binary distribution")
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)
PRINT_CEF_CONFIG()

# Pinned version + release date (version guard measures age from the date).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/cef-pin.txt" _pin REGEX "^CEF_RELEASE_DATE=")
string(REPLACE "CEF_RELEASE_DATE=" "" OS999_CEF_RELEASE_DATE "${_pin}")
add_compile_definitions(OS999_CEF_RELEASE_DATE="${OS999_CEF_RELEASE_DATE}")

add_executable(launch_policy_test tests/launch_policy_test.cc)
target_include_directories(launch_policy_test PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../include)
# Assertions are the test oracle, including Release configurations.
target_compile_options(launch_policy_test PRIVATE -UNDEBUG)
# Policy unit test (no CEF): run with ctest or directly.
add_executable(version_policy_test tests/version_policy_test.cc src/version_policy.cc)
target_include_directories(version_policy_test PRIVATE src)
target_compile_features(version_policy_test PRIVATE cxx_std_17)

if(OS_LINUX)
  # Linux / Raspberry Pi: one executable (CEF sub-processes reuse it), CPU
  # frames through the desktop's shared-memory bridge.
  set(CEF_TARGET "OS999Browser")
  ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
  SET_CEF_TARGET_OUT_DIR()
  set(OS999B_BRIDGE ${CMAKE_CURRENT_SOURCE_DIR}/../src/runtime_bridge.c)
  set_source_files_properties(${OS999B_BRIDGE} PROPERTIES
    COMPILE_DEFINITIONS _DEFAULT_SOURCE COMPILE_OPTIONS "-std=c11;-Wno-error")
  add_executable(${CEF_TARGET}
    src/os999_browser_linux.cc src/browser_app.cc src/browser_stats.cc
    src/embed/embed_browser.cc src/embed/embed_host.cc src/embed/embed_frames_linux.cc
    src/version_guard.cc src/version_policy.cc src/web_apps.cc ${WEB_RESOURCES_CC}
    ${OS999B_BRIDGE})
  SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_TARGET})
  add_dependencies(${CEF_TARGET} libcef_dll_wrapper)
  target_include_directories(${CEF_TARGET} PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../include)
  target_link_libraries(${CEF_TARGET} libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS} pthread rt)
  set_target_properties(${CEF_TARGET} PROPERTIES INSTALL_RPATH "$ORIGIN"
    BUILD_WITH_INSTALL_RPATH TRUE RUNTIME_OUTPUT_DIRECTORY ${CEF_TARGET_OUT_DIR})
  COPY_FILES("${CEF_TARGET}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
  COPY_FILES("${CEF_TARGET}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
  return()
endif()

if(NOT OS_MAC)
  message(FATAL_ERROR "OS999 Browser builds on macOS and Linux.")
endif()

set(OS999B_SRCS
  src/os999_browser_main.mm
  src/browser_app.cc
  src/web_apps.cc
  ${WEB_RESOURCES_CC}
  src/browser_app.h
  src/browser_client.cc
  src/browser_client.h
  src/browser_view.h
  src/browser_view.mm
  src/browser_compositor.h
  src/browser_compositor.mm
  src/chrome_bar.h
  src/chrome_bar.mm
  src/browser_stats.h
  src/browser_stats.cc
  src/embed/embed_browser.cc
  src/embed/embed_browser.h
  src/embed/embed_frames.h
  src/embed/embed_frames_mac.mm
  src/embed/embed_host.cc
  src/version_guard.cc
  src/version_policy.cc
  src/embed/embed_host.h
  )
set(OS999B_HELPER_SRCS src/process_helper_mac.cc)
set(OS999B_RESOURCES
  mac/Info.plist.in
  mac/os999browser.icns
  ${CMAKE_CURRENT_SOURCE_DIR}/../fonts/ChicagoFLF.ttf
  )

set(CEF_TARGET "OS999Browser")
set(CEF_HELPER_TARGET "OS999Browser_Helper")
set(CEF_HELPER_OUTPUT_NAME "OS999Browser Helper")
SET_CEF_TARGET_OUT_DIR()

list(APPEND CEF_COMPILER_FLAGS -fobjc-arc)
set(CEF_APP "${CEF_TARGET_OUT_DIR}/${CEF_TARGET}.app")
set(EXECUTABLE_NAME "${CEF_TARGET}")
set(PRODUCT_NAME "OS999 Browser Security")
set(VERSION_SHORT "0.1.0")

add_executable(${CEF_TARGET} MACOSX_BUNDLE ${OS999B_RESOURCES} ${OS999B_SRCS})
SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_TARGET})
add_dependencies(${CEF_TARGET} libcef_dll_wrapper)
target_include_directories(${CEF_TARGET} PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(${CEF_TARGET} libcef_dll_wrapper ${CEF_STANDARD_LIBS}
  "-framework Metal" "-framework QuartzCore" "-framework IOSurface"
  "-framework CoreText" "-framework CoreVideo")
set_target_properties(${CEF_TARGET} PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in
  CLANG_ENABLE_OBJC_ARC "YES")

# Use -n when replacing directory symlinks. CEF's ln -sf macro follows an
# existing directory symlink on incremental builds, creating recursive links
# inside Versions/A and breaking strict code-signature verification.
set(OS999_FRAMEWORK "${CEF_APP}/Contents/Frameworks/Chromium Embedded Framework.framework")
add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CEF_BINARY_DIR}/Chromium Embedded Framework.framework" "${OS999_FRAMEWORK}/Versions/A"
  COMMAND /bin/ln -sfn "Versions/A/Chromium Embedded Framework" "${OS999_FRAMEWORK}/Chromium Embedded Framework"
  COMMAND /bin/ln -sfn "Versions/A/Libraries" "${OS999_FRAMEWORK}/Libraries"
  COMMAND /bin/ln -sfn "Versions/A/Resources" "${OS999_FRAMEWORK}/Resources"
  COMMAND /bin/ln -sfn "A" "${OS999_FRAMEWORK}/Versions/Current"
  VERBATIM)



foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
  string(REPLACE ":" ";" _suffix_list ${_suffix_list})
  list(GET _suffix_list 0 _name_suffix)
  list(GET _suffix_list 1 _target_suffix)
  list(GET _suffix_list 2 _plist_suffix)
  set(_helper_target "${CEF_HELPER_TARGET}${_target_suffix}")
  set(_helper_output_name "${CEF_HELPER_OUTPUT_NAME}${_name_suffix}")
  set(_helper_info_plist "${CMAKE_CURRENT_BINARY_DIR}/helper-Info${_target_suffix}.plist")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/mac/helper-Info.plist.in" _plist_contents)
  string(REPLACE "\${EXECUTABLE_NAME}" "${_helper_output_name}" _plist_contents ${_plist_contents})
  string(REPLACE "\${PRODUCT_NAME}" "${_helper_output_name}" _plist_contents ${_plist_contents})
  string(REPLACE "\${BUNDLE_ID_SUFFIX}" "${_plist_suffix}" _plist_contents ${_plist_contents})
  file(WRITE ${_helper_info_plist} ${_plist_contents})
  add_executable(${_helper_target} MACOSX_BUNDLE ${OS999B_HELPER_SRCS})
  SET_EXECUTABLE_TARGET_PROPERTIES(${_helper_target})
  add_dependencies(${_helper_target} libcef_dll_wrapper)
  target_link_libraries(${_helper_target} libcef_dll_wrapper ${CEF_STANDARD_LIBS})
  set_target_properties(${_helper_target} PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${_helper_info_plist}
    OUTPUT_NAME ${_helper_output_name})
  add_dependencies(${CEF_TARGET} "${_helper_target}")
  add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CEF_TARGET_OUT_DIR}/${_helper_output_name}.app"
            "${CEF_APP}/Contents/Frameworks/${_helper_output_name}.app"
    VERBATIM)
endforeach()

COPY_MAC_RESOURCES("${OS999B_RESOURCES}" "mac/;fonts/" "${CEF_TARGET}"
  "${CMAKE_CURRENT_SOURCE_DIR}" "${CEF_APP}")
