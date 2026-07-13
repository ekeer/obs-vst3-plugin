cmake_minimum_required(VERSION 3.28...3.30)

option(ENABLE_VST3 "Enable building OBS with VST3 plugin" ON)

if(NOT ENABLE_VST3)
  target_disable(obs-vst3)
  return()
endif()

project(obs-vst3 VERSION 1.0.0)

find_package(Qt6 REQUIRED Widgets)
set(CMAKE_AUTOMOC ON)

add_library(obs-vst3 MODULE)
add_library(OBS::vst3 ALIAS obs-vst3)

# Add our cmake finder modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# --- Find VST3 SDK (via obs-deps or manual path) ---
find_package(VST3SDK REQUIRED)

message(STATUS "VST3 SDK found at: ${VST3SDK_PATH}")

# --- SDK compile warnings ---
set(
  SDK_WARN_FLAGS
  -Wno-cast-align
  -Wno-conversion
  -Wno-cpp
  -Wno-delete-non-virtual-dtor
  -Wno-deprecated
  -Wno-deprecated-copy-dtor
  -Wno-deprecated-declarations
  -Wno-dangling-else
  -Wno-extra
  -Wno-extra-semi
  -Wno-float-equal
  -Wno-format
  -Wno-format-security
  -Wno-format-truncation
  -Wno-ignored-qualifiers
  -Wno-int-to-pointer-cast
  -Wno-missing-braces
  -Wno-missing-field-initializers
  -Wno-non-virtual-dtor
  -Wno-overloaded-virtual
  -Wno-parentheses
  -Wno-pedantic
  -Wno-redundant-decls
  -Wno-reorder
  -Wno-shadow
  -Wno-sign-compare
  -Wno-sign-conversion
  -Wno-switch-default
  -Wno-type-limits
  -Wno-unused-but-set-variable
  -Wno-unused-function
  -Wno-unused-parameter
  -Wno-zero-as-null-pointer-constant
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(APPEND SDK_WARN_FLAGS -Wno-class-memaccess -Wno-maybe-uninitialized)
endif()

list(JOIN SDK_WARN_FLAGS " " SDK_WARN_FLAGS_STR)

# --- VST3 SDK sources ---
set(VST3_SDK_SOURCES)
foreach(_src IN LISTS VST3SDK_REQUIRED_FILES)
  list(APPEND VST3_SDK_SOURCES "${VST3SDK_PATH}/${_src}")
endforeach()

# --- Platform-specific scanner ---
if(OS_WINDOWS)
  set(VST3_SCANNER_PLATFORM src/VST3Scanner_Windows.cpp)
elseif(OS_MACOS)
  set(VST3_SCANNER_PLATFORM src/VST3Scanner_macOS.cpp)
elseif(OS_LINUX)
  set(VST3_SCANNER_PLATFORM src/VST3Scanner_Linux.cpp)
endif()

# --- Main plugin sources ---
set(
  VST3_MAIN_SOURCES
  src/plugin-main.cpp
  src/obs-vst3-module.cpp
  src/vst3-filter.cpp
  src/vst3-filter-audio.cpp
  src/vst3-filter-properties.cpp
  src/vst3-filter-save.cpp
  src/vst3-filter-sidechain.cpp
  src/VST3Plugin.cpp
  src/VST3Scanner.cpp
  ${VST3_SCANNER_PLATFORM}
  src/VST3HostApp.cpp
  src/VST3ComponentHolder.cpp
  src/VST3Graph.cpp
)

# --- Editor window sources ---
set(VST3EDITORWINDOW_SRC)
if(OS_WINDOWS)
  list(APPEND VST3EDITORWINDOW_SRC editor/win/VST3EditorWindow.cpp)
elseif(OS_MACOS)
  list(APPEND VST3EDITORWINDOW_SRC editor/mac/VST3EditorWindow.mm)
elseif(OS_LINUX)
  list(APPEND VST3EDITORWINDOW_SRC editor/linux/VST3EditorWindow.cpp editor/linux/RunLoopImpl.cpp)
endif()

# --- Header files ---
set(
  VST3_HEADERS
  include/obs-vst3.h
  include/vst3-filter.h
  include/vst3-filter-audio.h
  include/vst3-filter-properties.h
  include/vst3-filter-sidechain.h
  include/VST3Plugin.h
  include/VST3Scanner.h
  include/VST3HostApp.h
  include/VST3ComponentHolder.h
  include/VST3EditorWindow.h
  include/VST3Graph.h
)

# --- Add all sources ---
target_sources(
  obs-vst3 PRIVATE
  ${VST3_MAIN_SOURCES}
  ${VST3EDITORWINDOW_SRC}
  ${VST3_SDK_SOURCES}
  ${VST3_HEADERS}
)

# --- Disable warnings for SDK files ---
if(OS_LINUX OR OS_MACOS)
  set_source_files_properties(
    ${VST3_SDK_SOURCES}
    PROPERTIES COMPILE_FLAGS "${SDK_WARN_FLAGS_STR}"
  )
endif()

# --- macOS ARC ---
if(OS_MACOS)
  set_source_files_properties(
    editor/mac/VST3EditorWindow.mm
    "${VST3SDK_PATH}/public.sdk/source/vst/hosting/module_mac.mm"
    "${VST3SDK_PATH}/public.sdk/source/common/threadchecker_mac.mm"
    PROPERTIES
    COMPILE_OPTIONS "-fobjc-arc"
    COMPILE_FLAGS "${SDK_WARN_FLAGS_STR}"
  )
  target_link_libraries(
    obs-vst3 PRIVATE
    "-framework Cocoa" "-framework Foundation"
  )
endif()

# --- Include directories ---
target_include_directories(
  obs-vst3 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${VST3SDK_PATH}
  ${VST3SDK_PATH}/base
  ${VST3SDK_PATH}/pluginterfaces
  ${VST3SDK_PATH}/public.sdk/source/vst
  ${VST3SDK_PATH}/public.sdk/source/vst/hosting
  ${VST3SDK_PATH}/public.sdk/source/vst/utility
)

# --- Link libraries ---
target_link_libraries(
  obs-vst3 PRIVATE
  OBS::libobs
  Qt6::Widgets
)

# --- Windows resources ---
if(OS_WINDOWS)
  configure_file(cmake/windows/obs-module.rc.in obs-vst3.rc)
  target_sources(obs-vst3 PRIVATE obs-vst3.rc)
  target_link_options(obs-vst3 PRIVATE /DEF:${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows/obs-vst3.def)
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows/obs-studio.ico
    ${CMAKE_CURRENT_BINARY_DIR}/obs-studio.ico
    COPYONLY
  )
endif()

set_target_properties_obs(obs-vst3 PROPERTIES FOLDER plugins PREFIX "")
